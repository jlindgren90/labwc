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
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include "action.h"
#include "buffer.h"
#include "common/border.h"
#include "common/macros.h"
#include "common/mem.h"
#include "common/string-helpers.h"
#include "cycle.h"
#include "labwc.h"
#include "menu/menu.h"
#include "output.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "session-lock.h"
#include "ssd.h"
#include "theme.h"
#include "util.h"
#include "xwayland.h"

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

	struct wlr_xwayland_surface *xsurface =
		wlr_xwayland_surface_try_from_wlr_surface(surface);
	if (xsurface) {
		return xsurface->data;
	}

	return NULL;
}

struct wlr_surface *
view_get_surface(struct view *view)
{
	if (view->xdg_surface) {
		return view->xdg_surface->surface;
	}
	if (view->xwayland_surface) {
		return view->xwayland_surface->surface;
	}
	return NULL;
}

struct view *
view_get_root(struct view *view)
{
	assert(view);
	if (view->impl->get_root) {
		return view->impl->get_root(view);
	}
	return view;
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
	int x1 = 0;
	int y1 = 0;
	int x2 = usable.width;
	int y2 = usable.height;

	if (edge & LAB_EDGE_RIGHT) {
		x1 = (usable.width) / 2;
	}
	if (edge & LAB_EDGE_LEFT) {
		x2 = (usable.width) / 2;
	}
	if (edge & LAB_EDGE_BOTTOM) {
		y1 = (usable.height) / 2;
	}
	if (edge & LAB_EDGE_TOP) {
		y2 = (usable.height) / 2;
	}

	struct wlr_box dst = {
		.x = x1 + usable.x,
		.y = y1 + usable.y,
		.width = x2 - x1,
		.height = y2 - y1,
	};

	if (view) {
		struct border margin = ssd_get_margin(view);
		dst.x += margin.left;
		dst.y += margin.top;
		dst.width -= margin.left + margin.right;
		dst.height -= margin.top + margin.bottom;
	}

	return dst;
}

static bool
view_discover_output(struct view *view, const struct wlr_box *geometry)
{
	assert(view);

	if (!geometry) {
		geometry = &view->st->current;
	}

	struct output *output =
		output_nearest_to(geometry->x + geometry->width / 2,
			geometry->y + geometry->height / 2);

	if (output && output != view->st->output) {
		view_set_output(view->id, output);
		return true;
	}

	return false;
}

void
view_notify_active(struct view *view)
{
	ssd_set_active(view->ssd, view->st->active);
}

void
view_close(struct view *view)
{
	assert(view);
	if (view->impl->close) {
		view->impl->close(view);
	}
}

void
view_move(struct view *view, int x, int y)
{
	assert(view);
	view_move_resize(view->id, (struct wlr_box){
		.x = x, .y = y,
		.width = view->st->pending.width,
		.height = view->st->pending.height
	});
}

void
view_moved(struct view *view)
{
	assert(view);
	wlr_scene_node_set_position(&view->scene_tree->node,
		view->st->current.x, view->st->current.y);
	ssd_update_geometry(view->ssd);
	cursor_update_focus();
}

static void save_last_placement(struct view *view);

void
view_notify_move_resize(struct view *view)
{
	/*
	 * If the move/resize was user-initiated (rather than due to
	 * output layout change), then update the last placement info.
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
	if (view->st->minimized == minimized) {
		return;
	}

	view_set_minimized(view->id, minimized);
	view_update_visibility(view);

	/*
	 * Need to focus a different view when:
	 *   - minimizing the active view
	 *   - unminimizing any mapped view
	 */
	*need_refocus |=
		(minimized ? (view == g_server.active_view) : view->st->mapped);
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
	if (!output_is_usable(view->st->output)) {
		wlr_log(WLR_ERROR, "view has no output, not centering");
		return false;
	}

	struct border margin = ssd_get_margin(view);
	struct wlr_box usable =
		output_usable_area_in_layout_coords(view->st->output);
	int width = w + margin.left + margin.right;
	int height = h + margin.top + margin.bottom;

	/* If reference box is NULL then center to usable area */
	struct wlr_box centered =
		rect_center(width, height, ref ? *ref : usable);
	rect_move_within(&centered, usable);

	*x = centered.x + margin.left;
	*y = centered.y + margin.top;

	return true;
}

/* Make sure the passed-in view geometry is visible in view->output */
static bool
adjust_floating_geometry(struct view *view, struct wlr_box *geometry,
		bool midpoint_visibility)
{
	assert(view);

	if (!output_is_usable(view->st->output)) {
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
			view->st->output->wlr_output, geometry)) {
		/* Always make sure the titlebar starts within the usable area */
		struct border margin = ssd_get_margin(view);
		struct wlr_box usable =
			output_usable_area_in_layout_coords(view->st->output);

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
view_center(struct view *view, const struct wlr_box *ref)
{
	assert(view);
	int x, y;
	if (view_compute_centered_position(view, ref, view->st->pending.width,
			view->st->pending.height, &x, &y)) {
		view_move(view, x, y);
	}
}

void
view_constrain_size_to_that_of_usable_area(struct view *view)
{
	if (!view || !output_is_usable(view->st->output) || view->st->fullscreen) {
		return;
	}

	struct wlr_box usable_area =
		output_usable_area_in_layout_coords(view->st->output);
	struct border margin = ssd_get_margin(view);

	int available_width = usable_area.width - margin.left - margin.right;
	int available_height = usable_area.height - margin.top - margin.bottom;

	if (available_width <= 0 || available_height <= 0) {
		return;
	}

	if (available_height >= view->st->pending.height
			&& available_width >= view->st->pending.width) {
		return;
	}

	int width = MIN(view->st->pending.width, available_width);
	int height = MIN(view->st->pending.height, available_height);

	int right_edge = usable_area.x + usable_area.width;
	int bottom_edge = usable_area.y + usable_area.height;

	int x = MAX(usable_area.x + margin.left,
		MIN(view->st->pending.x, right_edge - width - margin.right));

	int y = MAX(usable_area.y + margin.top,
		MIN(view->st->pending.y, bottom_edge - height - margin.bottom));

	struct wlr_box box = {
		.x = x,
		.y = y,
		.width = width,
		.height = height,
	};
	view_move_resize(view->id, box);
}

void
view_apply_natural_geometry(struct view *view)
{
	assert(view);
	assert(view_is_floating(view->st));

	struct wlr_box geometry = view->st->natural_geom;
	/* Only adjust natural geometry if known (not 0x0) */
	if (!wlr_box_empty(&geometry)) {
		adjust_floating_geometry(view, &geometry,
			/* midpoint_visibility */ false);
	}
	view_move_resize(view->id, geometry);
}

static void
view_apply_tiled_geometry(struct view *view)
{
	assert(view);
	assert(view->st->tiled);
	assert(output_is_usable(view->st->output));

	view_move_resize(view->id, view_get_edge_snap_box(
		view, view->st->output, view->st->tiled));
}

static void
view_apply_fullscreen_geometry(struct view *view)
{
	assert(view);
	assert(view->st->fullscreen);
	assert(output_is_usable(view->st->output));

	struct wlr_box box = { 0 };
	wlr_output_layout_get_box(g_server.output_layout,
		view->st->output->wlr_output, &box);
	view_move_resize(view->id, box);
}

static void
view_apply_maximized_geometry(struct view *view)
{
	assert(view);
	assert(view->st->maximized != VIEW_AXIS_NONE);
	struct output *output = view->st->output;
	assert(output_is_usable(output));

	struct wlr_box box = output_usable_area_in_layout_coords(output);

	/*
	 * If one axis (horizontal or vertical) is unmaximized, it
	 * should use the natural geometry. But if that geometry is not
	 * on-screen on the output where the view is maximized, then
	 * center the unmaximized axis.
	 */
	struct wlr_box natural = view->st->natural_geom;
	if (view->st->maximized != VIEW_AXIS_BOTH
			&& !rect_intersects(box, natural)) {
		view_compute_centered_position(view, NULL,
			natural.width, natural.height,
			&natural.x, &natural.y);
	}

	if (view->st->ssd_enabled) {
		struct border border = ssd_get_margin(view);
		box.x += border.left;
		box.y += border.top;
		box.width -= border.right + border.left;
		box.height -= border.top + border.bottom;
	}

	if (view->st->maximized == VIEW_AXIS_VERTICAL) {
		box.x = natural.x;
		box.width = natural.width;
	} else if (view->st->maximized == VIEW_AXIS_HORIZONTAL) {
		box.y = natural.y;
		box.height = natural.height;
	}

	view_move_resize(view->id, box);
}

static void
view_apply_special_geometry(struct view *view)
{
	assert(view);
	assert(!view_is_floating(view->st));
	if (!output_is_usable(view->st->output)) {
		wlr_log(WLR_ERROR, "view has no output, not updating geometry");
		return;
	}

	if (view->st->fullscreen) {
		view_apply_fullscreen_geometry(view);
	} else if (view->st->maximized != VIEW_AXIS_NONE) {
		view_apply_maximized_geometry(view);
	} else if (view->st->tiled != LAB_EDGE_NONE) {
		view_apply_tiled_geometry(view);
	} else {
		assert(false); // not reached
	}
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

	if (view->st->maximized == axis) {
		return;
	}

	if (view->st->fullscreen) {
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
		view_store_natural_geom(view->id);
	}

	/*
	 * When natural geometry is unknown (0x0) for an xdg-shell view,
	 * we normally send a configure event of 0x0 to get the client's
	 * preferred size, but this doesn't work if unmaximizing only
	 * one axis. So in that corner case, set a fallback geometry.
	 */
	if ((axis == VIEW_AXIS_HORIZONTAL || axis == VIEW_AXIS_VERTICAL)
			&& wlr_box_empty(&view->st->natural_geom)) {
		view_set_natural_geom(view->id,
			view_get_fallback_natural_geometry(view));
	}

	view_set_maximized(view->id, axis);
	if (view_is_floating(view->st)) {
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
		view_maximize(view, view->st->maximized ^ axis);
		break;
	case VIEW_AXIS_BOTH:
		/*
		 * Maximize in both directions if unmaximized or partially
		 * maximized, otherwise unmaximize.
		 */
		view_maximize(view, (view->st->maximized == VIEW_AXIS_BOTH) ?
			VIEW_AXIS_NONE : VIEW_AXIS_BOTH);
		break;
	default:
		break;
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

void
view_notify_ssd_enabled(struct view *view)
{
	if (view->st->fullscreen) {
		return;
	}

	if (view->st->ssd_enabled) {
		decorate(view);
	} else {
		undecorate(view);
	}

	if (!view_is_floating(view->st)) {
		view_apply_special_geometry(view);
	}
}

void
view_toggle_fullscreen(struct view *view)
{
	assert(view);

	view_set_fullscreen(view, !view->st->fullscreen);
}

void
view_set_fullscreen(struct view *view, bool fullscreen)
{
	assert(view);
	if (fullscreen == view->st->fullscreen) {
		return;
	}
	if (fullscreen) {
		if (!output_is_usable(view->st->output)) {
			/* Prevent fullscreen with no available outputs */
			return;
		}
		/*
		 * Fullscreen via keybind or client request cancels
		 * interactive move/resize since we can't move/resize
		 * a fullscreen view.
		 */
		interactive_cancel(view);
		view_store_natural_geom(view->id);
	}

	view_set_fullscreen_internal(view->id, fullscreen);

	if (view->st->ssd_enabled) {
		if (fullscreen) {
			/* Hide decorations when going fullscreen */
			undecorate(view);
		} else {
			/* Re-show decorations when no longer fullscreen */
			decorate(view);
		}
	}

	if (view_is_floating(view->st)) {
		view_apply_natural_geometry(view);
	} else {
		view_apply_special_geometry(view);
	}

	/* Show fullscreen views above top-layer */
	desktop_update_top_layer_visibility();
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
	struct output *output = view->st->output;
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "cannot save last placement in unusable output");
		return;
	}
	if (!str_equal(view->last_placement.output_name, output->wlr_output->name)) {
		xstrdup_replace(view->last_placement.output_name,
			output->wlr_output->name);
	}
	view->last_placement.layout_geo = view->st->pending;
	view->last_placement.relative_geo = view->st->pending;
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
		view_set_output(view->id, output);
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

	if (!view_is_floating(view->st)) {
		view_apply_special_geometry(view);
	} else {
		/* Ensure view is on-screen */
		adjust_floating_geometry(view, &new_geo,
			/* midpoint_visibility */ true);
		view_move_resize(view->id, new_geo);
	}

	view->adjusting_for_layout_change = false;
}

enum view_axis
view_axis_parse(const char *direction)
{
	if (!direction) {
		return VIEW_AXIS_NONE;
	}
	if (!strcasecmp(direction, "horizontal")) {
		return VIEW_AXIS_HORIZONTAL;
	} else if (!strcasecmp(direction, "vertical")) {
		return VIEW_AXIS_VERTICAL;
	} else if (!strcasecmp(direction, "both")) {
		return VIEW_AXIS_BOTH;
	} else {
		return VIEW_AXIS_NONE;
	}
}

void
view_snap_to_edge(struct view *view, enum lab_edge edge)
{
	assert(view);

	if (view->st->fullscreen) {
		return;
	}

	struct output *output = view->st->output;
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "view has no output, not snapping to edge");
		return;
	}

	bool store_natural_geometry = !in_interactive_move(view);

	if (view->st->maximized != VIEW_AXIS_NONE) {
		/* Unmaximize + keep using existing natural_geometry */
		view_maximize(view, VIEW_AXIS_NONE);
	} else if (store_natural_geometry) {
		/* store current geometry as new natural_geometry */
		view_store_natural_geom(view->id);
	}
	view_set_output(view->id, output);
	view_set_tiled(view->id, edge);
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
view_notify_title_change(struct view *view)
{
	ssd_update_title(view->ssd);
}

static void
drop_icon_buffer(struct view *view)
{
	if (view->icon_buffer) {
		wlr_buffer_drop(&view->icon_buffer->base);
		view->icon_buffer = NULL;
	}
}

void
view_notify_app_id_change(struct view *view)
{
	drop_icon_buffer(view);
	ssd_update_icon(view->ssd);
}

void
view_reload_ssd(struct view *view)
{
	assert(view);
	drop_icon_buffer(view);

	if (view->st->ssd_enabled && !view->st->fullscreen) {
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

/* Used in both (un)map and (un)minimize */
void
view_update_visibility(struct view *view)
{
	bool visible = view->st->mapped && !view->st->minimized;
	if (visible == view->scene_tree->node.enabled) {
		return;
	}

	wlr_scene_node_set_enabled(&view->scene_tree->node, visible);

	/*
	 * Show top layer when a fullscreen view is hidden.
	 * Hide it if a fullscreen view is shown (or uncovered).
	 */
	desktop_update_top_layer_visibility();

	/* Update usable area to account for XWayland "struts" (panels) */
	if (view_has_strut_partial(view)) {
		output_update_all_usable_areas(false);
	}

	/* View might have been unmapped/minimized during move/resize */
	if (!visible) {
		interactive_cancel(view);
	}
}

struct lab_data_buffer *
view_get_icon_buffer(struct view *view)
{
	if (!view->icon_buffer) {
		view->icon_buffer =
			scaled_icon_buffer_load(view, g_theme.window_icon_size);
	}
	return view->icon_buffer;
}

void
view_set_icon(struct view *view, struct wl_array *buffers)
{
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

	drop_icon_buffer(view);
	ssd_update_icon(view->ssd);
}

void
view_init(struct view *view, bool is_xwayland)
{
	assert(view);

	view->id = view_add(view, is_xwayland);
	view->st = view_get_state(view->id);
	assert(view->st);
}

void
view_destroy(struct view *view)
{
	assert(view);

	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->destroy.link);

	cursor_on_view_destroy(view);

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

	/* TODO: call this on map/unmap instead */
	cycle_reinitialize();

	undecorate(view);

	clear_last_placement(view);
	view_set_icon(view, NULL);
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

	view_remove(view->id);

	/* Remove view from g_server.views */
	wl_list_remove(&view->link);
	free(view);

	cursor_update_focus();
}
