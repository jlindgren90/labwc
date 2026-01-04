// SPDX-License-Identifier: GPL-2.0-only
#include "view.h"
#include <assert.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "action.h"
#include "buffer.h"
#include "common/box.h"
#include "common/mem.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "cycle.h"
#include "foreign-toplevel/foreign.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "menu/menu.h"
#include "output.h"
#include "resize-indicator.h"
#include "session-lock.h"
#include "ssd.h"
#include "theme.h"
#include "wlr/util/log.h"

#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#include "xwayland.h"
#endif

struct view *
view_from_wlr_surface(struct wlr_surface *surface)
{
	assert(surface);
	/*
	 * TODO:
	 * - find a way to get rid of xdg/xwayland-specific stuff
	 * - look up root/toplevel surface if passed a subsurface?
	 */
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(surface);
	if (xdg_surface) {
		return xdg_surface->data;
	}
#if HAVE_XWAYLAND
	struct wlr_xwayland_surface *xsurface =
		wlr_xwayland_surface_try_from_wlr_surface(surface);
	if (xsurface) {
		return xsurface->data;
	}
#endif
	return NULL;
}

static struct view *
view_get_root(struct view *view)
{
	assert(view);
	if (view->impl->get_root) {
		return view->impl->get_root(view);
	}
	return view;
}

static bool
matches_criteria(struct view *view, enum lab_view_criteria criteria)
{
	if (!view_is_focusable(view)) {
		return false;
	}
	if (criteria & LAB_VIEW_CRITERIA_FULLSCREEN) {
		if (!view->fullscreen) {
			return false;
		}
	}
	if (criteria & LAB_VIEW_CRITERIA_ALWAYS_ON_TOP) {
		if (view->layer != VIEW_LAYER_ALWAYS_ON_TOP) {
			return false;
		}
	}
	if (criteria & LAB_VIEW_CRITERIA_NO_DIALOG) {
		if (view_is_modal_dialog(view)) {
			return false;
		}
	}
	if (criteria & LAB_VIEW_CRITERIA_NO_ALWAYS_ON_TOP) {
		if (view->layer == VIEW_LAYER_ALWAYS_ON_TOP) {
			return false;
		}
	}
	return true;
}

struct view *
view_next(struct wl_list *head, struct view *view, enum lab_view_criteria criteria)
{
	assert(head);

	struct wl_list *elm = view ? &view->link : head;

	for (elm = elm->next; elm != head; elm = elm->next) {
		view = wl_container_of(elm, view, link);
		if (matches_criteria(view, criteria)) {
			return view;
		}
	}
	return NULL;
}

struct view *
view_prev(struct wl_list *head, struct view *view, enum lab_view_criteria criteria)
{
	assert(head);

	struct wl_list *elm = view ? &view->link : head;

	for (elm = elm->prev; elm != head; elm = elm->prev) {
		view = wl_container_of(elm, view, link);
		if (matches_criteria(view, criteria)) {
			return view;
		}
	}
	return NULL;
}

void
view_array_append(struct wl_array *views,
		enum lab_view_criteria criteria)
{
	struct view *view;
	for_each_view(view, &g_server.views, criteria) {
		struct view **entry = wl_array_add(views, sizeof(*entry));
		if (!entry) {
			wlr_log(WLR_ERROR, "wl_array_add(): out of memory");
			continue;
		}
		*entry = view;
	}
}

enum view_wants_focus
view_wants_focus(struct view *view)
{
	assert(view);
	if (view->impl->wants_focus) {
		return view->impl->wants_focus(view);
	}
	return VIEW_WANTS_FOCUS_ALWAYS;
}

bool
view_is_focusable(struct view *view)
{
	assert(view);
	if (!view->surface) {
		return false;
	}

	switch (view_wants_focus(view)) {
	case VIEW_WANTS_FOCUS_ALWAYS:
	case VIEW_WANTS_FOCUS_LIKELY:
		return view->mapped;
	default:
		return false;
	}
}

void
view_offer_focus(struct view *view)
{
	assert(view);
	if (view->impl->offer_focus) {
		view->impl->offer_focus(view);
	}
}

/**
 * All view_apply_xxx_geometry() functions must *not* modify
 * any state besides repositioning or resizing the view.
 *
 * They may be called repeatably during output layout changes.
 */

struct wlr_box
view_get_edge_snap_box(struct view *view, struct output *output,
		enum lab_edge edge)
{
	struct wlr_box usable = output_usable_area_in_layout_coords(output);
	int x1 = rc.gap;
	int y1 = rc.gap;
	int x2 = usable.width - rc.gap;
	int y2 = usable.height - rc.gap;

	if (edge & LAB_EDGE_RIGHT) {
		x1 = (usable.width + rc.gap) / 2;
	}
	if (edge & LAB_EDGE_LEFT) {
		x2 = (usable.width - rc.gap) / 2;
	}
	if (edge & LAB_EDGE_BOTTOM) {
		y1 = (usable.height + rc.gap) / 2;
	}
	if (edge & LAB_EDGE_TOP) {
		y2 = (usable.height - rc.gap) / 2;
	}

	struct wlr_box dst = {
		.x = x1 + usable.x,
		.y = y1 + usable.y,
		.width = x2 - x1,
		.height = y2 - y1,
	};

	if (view) {
		struct border margin = ssd_get_margin(view->ssd);
		dst.x += margin.left;
		dst.y += margin.top;
		dst.width -= margin.left + margin.right;
		dst.height -= margin.top + margin.bottom;
	}

	return dst;
}

static bool
view_discover_output(struct view *view, struct wlr_box *geometry)
{
	assert(view);

	if (!geometry) {
		geometry = &view->current;
	}

	struct output *output =
		output_nearest_to(geometry->x + geometry->width / 2,
			geometry->y + geometry->height / 2);

	if (output && output != view->output) {
		view->output = output;
		return true;
	}

	return false;
}

void
view_set_activated(struct view *view, bool activated)
{
	assert(view);
	ssd_set_active(view->ssd, activated);
	if (view->impl->set_activated) {
		view->impl->set_activated(view, activated);
	}

	wl_signal_emit_mutable(&view->events.activated, &activated);

	if (rc.kb_layout_per_window) {
		if (!activated) {
			/* Store configured keyboard layout per view */
			view->keyboard_layout =
				g_seat.keyboard_group->keyboard.modifiers.group;
		} else {
			/* Switch to previously stored keyboard layout */
			keyboard_update_layout(view->keyboard_layout);
		}
	}
	output_set_has_fullscreen_view(view->output, view->fullscreen);
}

void
view_set_output(struct view *view, struct output *output)
{
	assert(view);
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "invalid output set for view");
		return;
	}
	view->output = output;
	/* Show fullscreen views above top-layer */
	if (view->fullscreen) {
		desktop_update_top_layer_visibility();
	}
}

void
view_close(struct view *view)
{
	assert(view);
	if (view->impl->close) {
		view->impl->close(view);
	}
}

static void
view_update_outputs(struct view *view)
{
	struct output *output;
	struct wlr_output_layout *layout = g_server.output_layout;

	uint64_t new_outputs = 0;
	wl_list_for_each(output, &g_server.outputs, link) {
		if (output_is_usable(output) && wlr_output_layout_intersects(
				layout, output->wlr_output, &view->current)) {
			new_outputs |= output->id_bit;
		}
	}

	if (new_outputs != view->outputs) {
		view->outputs = new_outputs;
		wl_signal_emit_mutable(&view->events.new_outputs, NULL);
		desktop_update_top_layer_visibility();
	}
}

bool
view_on_output(struct view *view, struct output *output)
{
	assert(view);
	assert(output);
	return (view->outputs & output->id_bit) != 0;
}

void
view_move(struct view *view, int x, int y)
{
	assert(view);
	view_move_resize(view, (struct wlr_box){
		.x = x, .y = y,
		.width = view->pending.width,
		.height = view->pending.height
	});
}

void
view_moved(struct view *view)
{
	assert(view);
	wlr_scene_node_set_position(&view->scene_tree->node,
		view->current.x, view->current.y);
	/*
	 * Only floating views change output when moved. Non-floating
	 * views (maximized/tiled/fullscreen) are tied to a particular
	 * output when they enter that state.
	 */
	if (view_is_floating(view)) {
		view_discover_output(view, NULL);
	}
	view_update_outputs(view);
	ssd_update_geometry(view->ssd);
	cursor_update_focus();
	if (rc.resize_indicator && g_server.grabbed_view == view) {
		resize_indicator_update(view);
	}
}

static void save_last_placement(struct view *view);

void
view_move_resize(struct view *view, struct wlr_box geo)
{
	assert(view);
	if (view->impl->configure) {
		view->impl->configure(view, geo);
	}

	/*
	 * If the move/resize was user-initiated (rather than due to
	 * output layout change), then update the last placement info.
	 *
	 * TODO: consider also updating view->output here for floating
	 * views (based on view->pending) rather than waiting until
	 * view_moved(). This might eliminate some race conditions with
	 * view_adjust_for_layout_change(), which uses view->pending.
	 * Not sure if it might have other side-effects though.
	 */
	if (!view->adjusting_for_layout_change) {
		save_last_placement(view);
	}
}

struct view_size_hints
view_get_size_hints(struct view *view)
{
	assert(view);
	if (view->impl->get_size_hints) {
		return view->impl->get_size_hints(view);
	}
	return (struct view_size_hints){0};
}

static void
substitute_nonzero(int *a, int *b)
{
	if (!(*a)) {
		*a = *b;
	} else if (!(*b)) {
		*b = *a;
	}
}

static int
round_to_increment(int val, int base, int inc)
{
	if (base < 0 || inc <= 0) {
		return val;
	}
	return base + (val - base + inc / 2) / inc * inc;
}

void
view_adjust_size(struct view *view, int *w, int *h)
{
	assert(view);
	struct view_size_hints hints = view_get_size_hints(view);

	/*
	 * "If a base size is not provided, the minimum size is to be
	 * used in its place and vice versa." (ICCCM 4.1.2.3)
	 */
	substitute_nonzero(&hints.min_width, &hints.base_width);
	substitute_nonzero(&hints.min_height, &hints.base_height);

	/*
	 * Snap width/height to requested size increments (if any).
	 * Typically, terminal emulators use these to make sure that the
	 * terminal is resized to a width/height evenly divisible by the
	 * cell (character) size.
	 */
	*w = round_to_increment(*w, hints.base_width, hints.width_inc);
	*h = round_to_increment(*h, hints.base_height, hints.height_inc);

	/* If a minimum width/height was not set, then use default */
	if (hints.min_width < 1) {
		hints.min_width = LAB_MIN_VIEW_WIDTH;
	}
	if (hints.min_height < 1) {
		hints.min_height = LAB_MIN_VIEW_HEIGHT;
	}
	*w = MAX(*w, hints.min_width);
	*h = MAX(*h, hints.min_height);
}

static void
_minimize(struct view *view, bool minimized, bool *need_refocus)
{
	assert(view);
	if (view->minimized == minimized) {
		return;
	}

	if (view->impl->minimize) {
		view->impl->minimize(view, minimized);
	}

	view->minimized = minimized;
	wl_signal_emit_mutable(&view->events.minimized, NULL);
	view_update_visibility(view);

	/*
	 * Need to focus a different view when:
	 *   - minimizing the active view
	 *   - unminimizing any mapped view
	 */
	*need_refocus |= (minimized ?
		(view == g_server.active_view) : view->mapped);
}

static void
view_append_children(struct view *view, struct wl_array *children)
{
	assert(view);
	if (view->impl->append_children) {
		view->impl->append_children(view, children);
	}
}

static void
minimize_sub_views(struct view *view, bool minimized, bool *need_refocus)
{
	struct view **child;
	struct wl_array children;

	wl_array_init(&children);
	view_append_children(view, &children);
	wl_array_for_each(child, &children) {
		_minimize(*child, minimized, need_refocus);
		minimize_sub_views(*child, minimized, need_refocus);
	}
	wl_array_release(&children);
}

/*
 * Minimize the whole view-hierarchy from top to bottom regardless of which one
 * in the hierarchy requested the minimize. For example, if an 'About' or
 * 'Open File' dialog is minimized, its toplevel is minimized also. And vice
 * versa.
 */
void
view_minimize(struct view *view, bool minimized)
{
	assert(view);
	bool need_refocus = false;

	if (g_server.input_mode == LAB_INPUT_STATE_CYCLE) {
		wlr_log(WLR_ERROR, "not minimizing window while window switching");
		return;
	}

	/*
	 * Minimize the root window first because some xwayland clients send a
	 * request-unmap to sub-windows at this point (for example gimp and its
	 * 'open file' dialog), so it saves trying to unmap them twice
	 */
	struct view *root = view_get_root(view);
	_minimize(root, minimized, &need_refocus);
	minimize_sub_views(root, minimized, &need_refocus);

	/*
	 * Update focus only at the end to avoid repeated focus changes.
	 * desktop_focus_view() will raise all sibling views together.
	 */
	if (need_refocus) {
		if (minimized) {
			desktop_focus_topmost_view();
		} else {
			desktop_focus_view(view, /* raise */ true);
		}
	}
}

bool
view_compute_centered_position(struct view *view, const struct wlr_box *ref,
		int w, int h, int *x, int *y)
{
	assert(view);
	if (w <= 0 || h <= 0) {
		wlr_log(WLR_ERROR, "view has empty geometry, not centering");
		return false;
	}
	if (!output_is_usable(view->output)) {
		wlr_log(WLR_ERROR, "view has no output, not centering");
		return false;
	}

	struct border margin = ssd_get_margin(view->ssd);
	struct wlr_box usable = output_usable_area_in_layout_coords(view->output);
	int width = w + margin.left + margin.right;
	int height = h + margin.top + margin.bottom;

	/* If reference box is NULL then center to usable area */
	box_center(width, height, ref ? ref : &usable, &usable, x, y);

	*x += margin.left;
	*y += margin.top;

	return true;
}

/* Make sure the passed-in view geometry is visible in view->output */
static bool
adjust_floating_geometry(struct view *view, struct wlr_box *geometry,
		bool midpoint_visibility)
{
	assert(view);

	if (!output_is_usable(view->output)) {
		wlr_log(WLR_ERROR, "view has no output, not positioning");
		return false;
	}

	/* Avoid moving panels out of their own reserved area ("strut") */
	if (view_has_strut_partial(view)) {
		return false;
	}

	bool adjusted = false;
	bool onscreen = false;
	if (wlr_output_layout_intersects(g_server.output_layout,
			view->output->wlr_output, geometry)) {
		/* Always make sure the titlebar starts within the usable area */
		struct border margin = ssd_get_margin(view->ssd);
		struct wlr_box usable =
			output_usable_area_in_layout_coords(view->output);

		if (geometry->x < usable.x + margin.left) {
			geometry->x = usable.x + margin.left;
			adjusted = true;
		}

		if (geometry->y < usable.y + margin.top) {
			geometry->y = usable.y + margin.top;
			adjusted = true;
		}

		if (!midpoint_visibility) {
			/*
			 * If midpoint visibility is not required, the view is
			 * on screen if at least one pixel is visible.
			 */
			onscreen = true;
		} else {
			/* Otherwise, make sure the midpoint is on screen */
			int mx = geometry->x + geometry->width / 2;
			int my = geometry->y + geometry->height / 2;

			onscreen = mx <= usable.x + usable.width &&
				my <= usable.y + usable.height;
		}
	}

	if (onscreen) {
		return adjusted;
	}

	return view_compute_centered_position(view, NULL, geometry->width,
		geometry->height, &geometry->x, &geometry->y);
}

struct wlr_box
view_get_fallback_natural_geometry(struct view *view)
{
	struct wlr_box box = {
		.width = VIEW_FALLBACK_WIDTH,
		.height = VIEW_FALLBACK_HEIGHT,
	};
	view_compute_centered_position(view, NULL,
		box.width, box.height, &box.x, &box.y);
	return box;
}

void
view_store_natural_geometry(struct view *view)
{
	assert(view);
	/*
	 * Do not overwrite the stored geometry if fullscreen or tiled.
	 * Maximized views are handled on a per-axis basis (see below).
	 */
	if (view->fullscreen || view_is_tiled(view)) {
		return;
	}

	/*
	 * Note that for xdg-shell views that start fullscreen or maximized,
	 * we end up storing a natural geometry of 0x0. This is intentional.
	 * When leaving fullscreen or unmaximizing, we pass 0x0 to the
	 * xdg-toplevel configure event, which means the application should
	 * choose its own size.
	 */
	if (!(view->maximized & VIEW_AXIS_HORIZONTAL)) {
		view->natural_geometry.x = view->pending.x;
		view->natural_geometry.width = view->pending.width;
	}
	if (!(view->maximized & VIEW_AXIS_VERTICAL)) {
		view->natural_geometry.y = view->pending.y;
		view->natural_geometry.height = view->pending.height;
	}
}

void
view_center(struct view *view, const struct wlr_box *ref)
{
	assert(view);
	int x, y;
	if (view_compute_centered_position(view, ref, view->pending.width,
			view->pending.height, &x, &y)) {
		view_move(view, x, y);
	}
}

void
view_constrain_size_to_that_of_usable_area(struct view *view)
{
	if (!view || !output_is_usable(view->output) || view->fullscreen) {
		return;
	}

	struct wlr_box usable_area =
			output_usable_area_in_layout_coords(view->output);
	struct border margin = ssd_get_margin(view->ssd);

	int available_width = usable_area.width - margin.left - margin.right;
	int available_height = usable_area.height - margin.top - margin.bottom;

	if (available_width <= 0 || available_height <= 0) {
		return;
	}

	if (available_height >= view->pending.height &&
			available_width >= view->pending.width) {
		return;
	}

	int width = MIN(view->pending.width, available_width);
	int height = MIN(view->pending.height, available_height);

	int right_edge = usable_area.x + usable_area.width;
	int bottom_edge = usable_area.y + usable_area.height;

	int x =
		MAX(usable_area.x + margin.left,
			MIN(view->pending.x, right_edge - width - margin.right));

	int y =
		MAX(usable_area.y + margin.top,
			MIN(view->pending.y, bottom_edge - height - margin.bottom));

	struct wlr_box box = {
		.x = x,
		.y = y,
		.width = width,
		.height = height,
	};
	view_move_resize(view, box);
}

void
view_apply_natural_geometry(struct view *view)
{
	assert(view);
	assert(view_is_floating(view));

	struct wlr_box geometry = view->natural_geometry;
	/* Only adjust natural geometry if known (not 0x0) */
	if (!wlr_box_empty(&geometry)) {
		adjust_floating_geometry(view, &geometry,
			/* midpoint_visibility */ false);
	}
	view_move_resize(view, geometry);
}

static void
view_apply_tiled_geometry(struct view *view)
{
	assert(view);
	assert(view->tiled);
	assert(output_is_usable(view->output));

	view_move_resize(view, view_get_edge_snap_box(view,
		view->output, view->tiled));
}

static void
view_apply_fullscreen_geometry(struct view *view)
{
	assert(view);
	assert(view->fullscreen);
	assert(output_is_usable(view->output));

	struct wlr_box box = { 0 };
	wlr_output_layout_get_box(g_server.output_layout,
		view->output->wlr_output, &box);
	view_move_resize(view, box);
}

static void
view_apply_maximized_geometry(struct view *view)
{
	assert(view);
	assert(view->maximized != VIEW_AXIS_NONE);
	struct output *output = view->output;
	assert(output_is_usable(output));

	struct wlr_box box = output_usable_area_in_layout_coords(output);

	/*
	 * If one axis (horizontal or vertical) is unmaximized, it
	 * should use the natural geometry. But if that geometry is not
	 * on-screen on the output where the view is maximized, then
	 * center the unmaximized axis.
	 */
	struct wlr_box natural = view->natural_geometry;
	if (view->maximized != VIEW_AXIS_BOTH
			&& !box_intersects(&box, &natural)) {
		view_compute_centered_position(view, NULL,
			natural.width, natural.height,
			&natural.x, &natural.y);
	}

	if (view->ssd_mode) {
		struct border border = ssd_thickness(view);
		box.x += border.left;
		box.y += border.top;
		box.width -= border.right + border.left;
		box.height -= border.top + border.bottom;
	}

	if (view->maximized == VIEW_AXIS_VERTICAL) {
		box.x = natural.x;
		box.width = natural.width;
	} else if (view->maximized == VIEW_AXIS_HORIZONTAL) {
		box.y = natural.y;
		box.height = natural.height;
	}

	view_move_resize(view, box);
}

static void
view_apply_special_geometry(struct view *view)
{
	assert(view);
	assert(!view_is_floating(view));
	if (!output_is_usable(view->output)) {
		wlr_log(WLR_ERROR, "view has no output, not updating geometry");
		return;
	}

	if (view->fullscreen) {
		view_apply_fullscreen_geometry(view);
	} else if (view->maximized != VIEW_AXIS_NONE) {
		view_apply_maximized_geometry(view);
	} else if (view->tiled) {
		view_apply_tiled_geometry(view);
	} else {
		assert(false); // not reached
	}
}

/*
 * Sets maximized state without updating geometry. Used in interactive
 * move/resize. In most other cases, use view_maximize() instead.
 */
void
view_set_maximized(struct view *view, enum view_axis maximized)
{
	assert(view);
	if (view->maximized == maximized) {
		return;
	}

	if (view->impl->maximize) {
		view->impl->maximize(view, maximized);
	}

	view->maximized = maximized;
	wl_signal_emit_mutable(&view->events.maximized, NULL);

	/*
	 * Ensure that follow-up actions like SnapToEdge / SnapToRegion
	 * use up-to-date SSD margin information. Otherwise we will end
	 * up using an outdated ssd->margin to calculate offsets.
	 */
	ssd_update_margin(view->ssd);
}

bool
view_is_tiled(struct view *view)
{
	assert(view);
	return view->tiled != LAB_EDGE_NONE;
}

bool
view_is_floating(struct view *view)
{
	assert(view);
	return !(view->fullscreen || (view->maximized != VIEW_AXIS_NONE)
		|| view_is_tiled(view));
}

static void
view_notify_tiled(struct view *view)
{
	assert(view);
	if (view->impl->notify_tiled) {
		view->impl->notify_tiled(view);
	}
}

/* Reset tiled state of view without changing geometry */
void
view_set_untiled(struct view *view)
{
	assert(view);
	view->tiled = LAB_EDGE_NONE;
	view_notify_tiled(view);
}

static bool
in_interactive_move(struct view *view)
{
	return (g_server.input_mode == LAB_INPUT_STATE_MOVE
		&& g_server.grabbed_view == view);
}

void
view_maximize(struct view *view, enum view_axis axis)
{
	assert(view);

	if (view->maximized == axis) {
		return;
	}

	if (view->fullscreen) {
		return;
	}

	bool store_natural_geometry = !in_interactive_move(view);

	/*
	 * Maximize/unmaximize via keybind or client request cancels
	 * interactive move/resize.
	 */
	interactive_cancel(view);

	/*
	 * Update natural geometry for any axis that wasn't already
	 * maximized. This is needed even when unmaximizing, because in
	 * single-axis cases the client may have resized the other axis
	 * while one axis was maximized.
	 */
	if (store_natural_geometry) {
		view_store_natural_geometry(view);
	}

	/*
	 * When natural geometry is unknown (0x0) for an xdg-shell view,
	 * we normally send a configure event of 0x0 to get the client's
	 * preferred size, but this doesn't work if unmaximizing only
	 * one axis. So in that corner case, set a fallback geometry.
	 */
	if ((axis == VIEW_AXIS_HORIZONTAL || axis == VIEW_AXIS_VERTICAL)
			&& wlr_box_empty(&view->natural_geometry)) {
		view->natural_geometry = view_get_fallback_natural_geometry(view);
	}

	view_set_maximized(view, axis);
	if (view_is_floating(view)) {
		view_apply_natural_geometry(view);
	} else {
		view_apply_special_geometry(view);
	}
}

void
view_toggle_maximize(struct view *view, enum view_axis axis)
{
	assert(view);
	switch (axis) {
	case VIEW_AXIS_HORIZONTAL:
	case VIEW_AXIS_VERTICAL:
		/* Toggle one axis (XOR) */
		view_maximize(view, view->maximized ^ axis);
		break;
	case VIEW_AXIS_BOTH:
		/*
		 * Maximize in both directions if unmaximized or partially
		 * maximized, otherwise unmaximize.
		 */
		view_maximize(view, (view->maximized == VIEW_AXIS_BOTH) ?
			VIEW_AXIS_NONE : VIEW_AXIS_BOTH);
		break;
	default:
		break;
	}
}

bool
view_wants_decorations(struct view *view)
{
	/*
	 * view->ssd_preference may be set by the decoration implementation
	 * e.g. src/decorations/xdg-deco.c or src/decorations/kde-deco.c.
	 */
	switch (view->ssd_preference) {
	case LAB_SSD_PREF_SERVER:
		return true;
	case LAB_SSD_PREF_CLIENT:
		return false;
	default:
		/*
		 * We don't know anything about the client preference
		 * so fall back to core.decoration settings in rc.xml
		 */
		return rc.xdg_shell_server_side_deco;
	}
}

void
view_set_layer(struct view *view, enum view_layer layer)
{
	assert(view);
	view->layer = layer;
	wlr_scene_node_reparent(&view->scene_tree->node,
		g_server.view_trees[layer]);
}

void
view_toggle_always_on_top(struct view *view)
{
	assert(view);
	if (view->layer == VIEW_LAYER_ALWAYS_ON_TOP) {
		view_set_layer(view, VIEW_LAYER_NORMAL);
	} else {
		view_set_layer(view, VIEW_LAYER_ALWAYS_ON_TOP);
	}
}

static void
decorate(struct view *view)
{
	if (!view->ssd) {
		view->ssd = ssd_create(view,
			view == g_server.active_view);
	}
}

static void
undecorate(struct view *view)
{
	ssd_destroy(view->ssd);
	view->ssd = NULL;
}

bool
view_titlebar_visible(struct view *view)
{
	if (view->maximized == VIEW_AXIS_BOTH
			&& rc.hide_maximized_window_titlebar) {
		return false;
	}
	return view->ssd_mode == LAB_SSD_MODE_FULL;
}

void
view_set_ssd_mode(struct view *view, enum lab_ssd_mode mode)
{
	assert(view);

	if (view->fullscreen || mode == view->ssd_mode) {
		return;
	}

	/*
	 * Set these first since they are referenced
	 * within the call tree of ssd_create() and ssd_thickness()
	 */
	view->ssd_mode = mode;

	if (mode) {
		decorate(view);
		ssd_set_titlebar(view->ssd, view_titlebar_visible(view));
	} else {
		undecorate(view);
	}

	if (!view_is_floating(view)) {
		view_apply_special_geometry(view);
	}
}

void
view_toggle_fullscreen(struct view *view)
{
	assert(view);

	view_set_fullscreen(view, !view->fullscreen);
}

/* For internal use only. Does not update geometry. */
static void
set_fullscreen(struct view *view, bool fullscreen)
{
	/* Hide decorations when going fullscreen */
	if (fullscreen && view->ssd_mode) {
		undecorate(view);
	}

	if (view->impl->set_fullscreen) {
		view->impl->set_fullscreen(view, fullscreen);
	}

	view->fullscreen = fullscreen;
	wl_signal_emit_mutable(&view->events.fullscreened, NULL);

	/* Re-show decorations when no longer fullscreen */
	if (!fullscreen && view->ssd_mode) {
		decorate(view);
	}

	/* Show fullscreen views above top-layer */
	if (view->output) {
		desktop_update_top_layer_visibility();
	}
}

void
view_set_fullscreen(struct view *view, bool fullscreen)
{
	assert(view);
	if (fullscreen == view->fullscreen) {
		return;
	}
	if (fullscreen) {
		if (!output_is_usable(view->output)) {
			/* Prevent fullscreen with no available outputs */
			return;
		}
		/*
		 * Fullscreen via keybind or client request cancels
		 * interactive move/resize since we can't move/resize
		 * a fullscreen view.
		 */
		interactive_cancel(view);
		view_store_natural_geometry(view);
	}

	set_fullscreen(view, fullscreen);
	if (view_is_floating(view)) {
		view_apply_natural_geometry(view);
	} else {
		view_apply_special_geometry(view);
	}
	output_set_has_fullscreen_view(view->output, view->fullscreen);
	/*
	 * Entering/leaving fullscreen might result in a different
	 * scene node ending up under the cursor even if view_moved()
	 * isn't called. Update cursor focus explicitly for that case.
	 */
	cursor_update_focus();
}

static void
save_last_placement(struct view *view)
{
	assert(view);
	struct output *output = view->output;
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "cannot save last placement in unusable output");
		return;
	}
	if (!str_equal(view->last_placement.output_name, output->wlr_output->name)) {
		xstrdup_replace(view->last_placement.output_name,
			output->wlr_output->name);
	}
	view->last_placement.layout_geo = view->pending;
	view->last_placement.relative_geo = view->pending;
	view->last_placement.relative_geo.x -= output->scene_output->x;
	view->last_placement.relative_geo.y -= output->scene_output->y;
}

static void
clear_last_placement(struct view *view)
{
	assert(view);
	zfree(view->last_placement.output_name);
	view->last_placement.relative_geo = (struct wlr_box){0};
	view->last_placement.layout_geo = (struct wlr_box){0};
}

void
view_adjust_for_layout_change(struct view *view)
{
	assert(view);
	if (wlr_box_empty(&view->last_placement.layout_geo)) {
		/* Not using assert() just in case */
		wlr_log(WLR_ERROR, "view has no last placement info");
		return;
	}

	view->adjusting_for_layout_change = true;

	struct wlr_box new_geo;
	struct output *output = output_from_name(view->last_placement.output_name);
	if (output_is_usable(output)) {
		/*
		 * When the previous output (which might have been reconnected
		 * or relocated) is available, keep the relative position on it.
		 */
		new_geo = view->last_placement.relative_geo;
		new_geo.x += output->scene_output->x;
		new_geo.y += output->scene_output->y;
		view->output = output;
	} else {
		/*
		 * Otherwise, evacuate the view to another output. Use the last
		 * layout geometry so that the view position is kept when the
		 * user reconnects the previous output in a different connector
		 * or the reconnected output somehow gets a different name.
		 */
		view_discover_output(view, &view->last_placement.layout_geo);
		new_geo = view->last_placement.layout_geo;
	}

	if (!view_is_floating(view)) {
		view_apply_special_geometry(view);
	} else {
		/* Ensure view is on-screen */
		adjust_floating_geometry(view, &new_geo,
			/* midpoint_visibility */ true);
		view_move_resize(view, new_geo);
	}

	view_update_outputs(view);
	view->adjusting_for_layout_change = false;
}

void
view_on_output_destroy(struct view *view)
{
	assert(view);
	view->output = NULL;
}

enum view_axis
view_axis_parse(const char *direction)
{
	if (!direction) {
		return VIEW_AXIS_INVALID;
	}
	if (!strcasecmp(direction, "horizontal")) {
		return VIEW_AXIS_HORIZONTAL;
	} else if (!strcasecmp(direction, "vertical")) {
		return VIEW_AXIS_VERTICAL;
	} else if (!strcasecmp(direction, "both")) {
		return VIEW_AXIS_BOTH;
	} else if (!strcasecmp(direction, "none")) {
		return VIEW_AXIS_NONE;
	} else {
		return VIEW_AXIS_INVALID;
	}
}

void
view_snap_to_edge(struct view *view, enum lab_edge edge,
		bool across_outputs, bool combine)
{
	assert(view);

	if (view->fullscreen) {
		return;
	}

	struct output *output = view->output;
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "view has no output, not snapping to edge");
		return;
	}

	bool store_natural_geometry = !in_interactive_move(view);

	if (lab_edge_is_cardinal(edge) && view->maximized == VIEW_AXIS_NONE
			&& view->tiled != LAB_EDGE_CENTER) {
		enum lab_edge invert_edge = lab_edge_invert(edge);
		/* Represents axis of snapping direction */
		enum lab_edge parallel_mask = edge | invert_edge;
		/*
		 * The vector view->tiled is split to components
		 * parallel/orthogonal to snapping direction. For example,
		 * view->tiled=TOP_LEFT is split to parallel_tiled=TOP and
		 * orthogonal_tiled=LEFT when edge=TOP or edge=BOTTOM.
		 */
		enum lab_edge parallel_tiled = view->tiled & parallel_mask;
		enum lab_edge orthogonal_tiled = view->tiled & ~parallel_mask;

		if (across_outputs && view->tiled == edge) {
			/*
			 * E.g. when window is tiled to up and being snapped
			 * to up again, move it to the output above and tile
			 * it to down.
			 */
			output = output_get_adjacent(view->output, edge,
				/* wrap */ false);
			if (!output_is_usable(output)) {
				return;
			}
			edge = invert_edge;
		} else if (combine && parallel_tiled == invert_edge
				&& orthogonal_tiled != LAB_EDGE_NONE) {
			/*
			 * E.g. when window is tiled to downleft/downright and
			 * being snapped to up, tile it to left/right.
			 */
			edge = view->tiled & ~parallel_mask;
		} else if (combine && parallel_tiled == LAB_EDGE_NONE) {
			/*
			 * E.g. when window is tiled to left/right and being
			 * snapped to up, tile it to upleft/upright.
			 */
			edge = view->tiled | edge;
		}
	}

	if (view->maximized != VIEW_AXIS_NONE) {
		/* Unmaximize + keep using existing natural_geometry */
		view_maximize(view, VIEW_AXIS_NONE);
	} else if (store_natural_geometry) {
		/* store current geometry as new natural_geometry */
		view_store_natural_geometry(view);
	}
	view_set_untiled(view);
	view_set_output(view, output);
	view->tiled = edge;
	view_notify_tiled(view);
	view_apply_tiled_geometry(view);
}

static void
for_each_subview(struct view *view, void (*action)(struct view *))
{
	struct wl_array subviews;
	struct view **subview;

	wl_array_init(&subviews);
	view_append_children(view, &subviews);
	wl_array_for_each(subview, &subviews) {
		action(*subview);
	}
	wl_array_release(&subviews);
}

static void
move_to_front(struct view *view)
{
	wl_list_remove(&view->link);
	wl_list_insert(&g_server.views, &view->link);
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
}

/*
 * In the view_move_to_{front,back} functions, a modal dialog is always
 * shown above its parent window, and the two always move together, so
 * other windows cannot come between them.
 * This is consistent with GTK3/Qt5 applications on mutter and openbox.
 */
void
view_move_to_front(struct view *view)
{
	assert(view);
	assert(!wl_list_empty(&g_server.views));

	/*
	 * Check whether the view is already in front, or is the root
	 * parent of the view in front (in which case we don't want to
	 * raise it in front of its sub-view).
	 */
	struct view *front = wl_container_of(g_server.views.next, front, link);
	if (view == front || view == view_get_root(front)) {
		return;
	}

	struct view *root = view_get_root(view);
	assert(root);

	move_to_front(root);
	for_each_subview(root, move_to_front);
	/* make sure view is in front of other sub-views */
	if (view != root) {
		move_to_front(view);
	}

	cursor_update_focus();
	desktop_update_top_layer_visibility();
}

bool
view_is_modal_dialog(struct view *view)
{
	assert(view);
	assert(view->impl->is_modal_dialog);
	return view->impl->is_modal_dialog(view);
}

struct view *
view_get_modal_dialog(struct view *view)
{
	assert(view);
	/* check view itself first */
	if (view_is_modal_dialog(view)) {
		return view;
	}

	/* check sibling views */
	struct view *dialog = NULL;
	struct view *root = view_get_root(view);
	struct wl_array children;
	struct view **child;

	wl_array_init(&children);
	view_append_children(root, &children);
	wl_array_for_each(child, &children) {
		if (view_is_modal_dialog(*child)) {
			dialog = *child;
			break;
		}
	}
	wl_array_release(&children);
	return dialog;
}

bool
view_has_strut_partial(struct view *view)
{
	assert(view);
	return view->impl->has_strut_partial &&
		view->impl->has_strut_partial(view);
}

void
view_set_title(struct view *view, const char *title)
{
	assert(view);
	if (!title) {
		title = "";
	}

	if (!strcmp(view->title, title)) {
		return;
	}
	xstrdup_replace(view->title, title);

	ssd_update_title(view->ssd);
	wl_signal_emit_mutable(&view->events.new_title, NULL);
}

void
view_set_app_id(struct view *view, const char *app_id)
{
	assert(view);
	if (!app_id) {
		app_id = "";
	}

	if (!strcmp(view->app_id, app_id)) {
		return;
	}
	xstrdup_replace(view->app_id, app_id);

	wl_signal_emit_mutable(&view->events.new_app_id, NULL);
}

void
view_reload_ssd(struct view *view)
{
	assert(view);
	if (view->ssd_mode && !view->fullscreen) {
		undecorate(view);
		decorate(view);
	}
}

void
view_toggle_keybinds(struct view *view)
{
	assert(view);
	view->inhibits_keybinds = !view->inhibits_keybinds;
}

bool
view_inhibits_actions(struct view *view, struct wl_list *actions)
{
	return view && view->inhibits_keybinds && !actions_contain_toggle_keybinds(actions);
}

void
mappable_connect(struct mappable *mappable, struct wlr_surface *surface,
		wl_notify_func_t notify_map, wl_notify_func_t notify_unmap)
{
	assert(mappable);
	assert(!mappable->connected);
	mappable->map.notify = notify_map;
	wl_signal_add(&surface->events.map, &mappable->map);
	mappable->unmap.notify = notify_unmap;
	wl_signal_add(&surface->events.unmap, &mappable->unmap);
	mappable->connected = true;
}

void
mappable_disconnect(struct mappable *mappable)
{
	assert(mappable);
	assert(mappable->connected);
	wl_list_remove(&mappable->map.link);
	wl_list_remove(&mappable->unmap.link);
	mappable->connected = false;
}

/* Used in both (un)map and (un)minimize */
void
view_update_visibility(struct view *view)
{
	bool visible = view->mapped && !view->minimized;
	if (visible == view->scene_tree->node.enabled) {
		return;
	}

	wlr_scene_node_set_enabled(&view->scene_tree->node, visible);

	/*
	 * Show top layer when a fullscreen view is hidden.
	 * Hide it if a fullscreen view is shown (or uncovered).
	 */
	desktop_update_top_layer_visibility();

	/*
	 * We may need to disable adaptive sync if view was fullscreen.
	 *
	 * FIXME: this logic doesn't account for multiple fullscreen
	 * views. It should probably be combined with the existing
	 * logic in desktop_update_top_layer_visibility().
	 */
	if (view->fullscreen && !visible) {
		output_set_has_fullscreen_view(view->output, false);
	}

	/* Update usable area to account for XWayland "struts" (panels) */
	if (view_has_strut_partial(view)) {
		output_update_all_usable_areas(false);
	}

	/* View might have been unmapped/minimized during move/resize */
	if (!visible) {
		interactive_cancel(view);
	}
}

void
view_set_icon(struct view *view, const char *icon_name, struct wl_array *buffers)
{
	/* Update icon name */
	zfree(view->icon.name);
	if (icon_name) {
		view->icon.name = xstrdup(icon_name);
	}

	/* Update icon images */
	struct lab_data_buffer **buffer;
	wl_array_for_each(buffer, &view->icon.buffers) {
		wlr_buffer_drop(&(*buffer)->base);
	}
	wl_array_release(&view->icon.buffers);
	wl_array_init(&view->icon.buffers);
	if (buffers) {
		wl_array_copy(&view->icon.buffers, buffers);
	}

	wl_signal_emit_mutable(&view->events.set_icon, NULL);
}

void
view_init(struct view *view)
{
	assert(view);

	wl_signal_init(&view->events.new_app_id);
	wl_signal_init(&view->events.new_title);
	wl_signal_init(&view->events.new_outputs);
	wl_signal_init(&view->events.maximized);
	wl_signal_init(&view->events.minimized);
	wl_signal_init(&view->events.fullscreened);
	wl_signal_init(&view->events.activated);
	wl_signal_init(&view->events.set_icon);
	wl_signal_init(&view->events.destroy);

	view->title = xstrdup("");
	view->app_id = xstrdup("");
}

void
view_destroy(struct view *view)
{
	assert(view);

	wl_signal_emit_mutable(&view->events.destroy, NULL);

	if (view->mappable.connected) {
		mappable_disconnect(&view->mappable);
	}

	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->destroy.link);

	zfree(view->title);
	zfree(view->app_id);

	if (view->foreign_toplevel) {
		foreign_toplevel_destroy(view->foreign_toplevel);
		view->foreign_toplevel = NULL;
	}

	/*
	 * This check is (in theory) redundant since interactive_cancel()
	 * is called at unmap. Leaving it here just to be sure.
	 */
	if (g_server.grabbed_view == view) {
		interactive_cancel(view);
	}

	if (g_server.active_view == view) {
		g_server.active_view = NULL;
	}

	if (g_server.session_lock_manager->last_active_view == view) {
		g_server.session_lock_manager->last_active_view = NULL;
	}

	/* TODO: call this on map/unmap instead */
	cycle_reinitialize();

	undecorate(view);

	clear_last_placement(view);
	view_set_icon(view, NULL, NULL);
	menu_on_view_destroy(view);

	/*
	 * Destroy the view's scene tree. View methods assume this is non-NULL,
	 * so we should avoid any calls to those between this and freeing the
	 * view.
	 */
	if (view->scene_tree) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	assert(wl_list_empty(&view->events.new_app_id.listener_list));
	assert(wl_list_empty(&view->events.new_title.listener_list));
	assert(wl_list_empty(&view->events.new_outputs.listener_list));
	assert(wl_list_empty(&view->events.maximized.listener_list));
	assert(wl_list_empty(&view->events.minimized.listener_list));
	assert(wl_list_empty(&view->events.fullscreened.listener_list));
	assert(wl_list_empty(&view->events.activated.listener_list));
	assert(wl_list_empty(&view->events.set_icon.listener_list));
	assert(wl_list_empty(&view->events.destroy.listener_list));

	/* Remove view from g_server.views */
	wl_list_remove(&view->link);
	free(view);

	cursor_update_focus();
}
