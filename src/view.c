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
#include "common/macros.h"
#include "cycle.h"
#include "labwc.h"
#include "menu/menu.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "session-lock.h"
#include "ssd.h"
#include "theme.h"
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
view_append_children(struct view *view, struct wl_array *children)
{
	assert(view);
	if (view->impl->append_children) {
		view->impl->append_children(view, children);
	}
}

void
view_notify_minimize(struct view *view, bool minimized)
{
	assert(view);

	/*
	 * Update focus only at the end to avoid repeated focus changes.
	 * desktop_focus_view() will raise all sibling views together.
	 */
	if (minimized) {
		// Check if active view was minimized (even by a sibling)
		if (g_server.active_view && g_server.active_view->st->minimized) {
			desktop_focus_topmost_view();
		}
	} else {
		desktop_focus_view(view, /* raise */ true);
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
		view_maximize(view->id, view->st->maximized ^ axis);
		break;
	case VIEW_AXIS_BOTH:
		/*
		 * Maximize in both directions if unmaximized or partially
		 * maximized, otherwise unmaximize.
		 */
		view_maximize(view->id, (view->st->maximized == VIEW_AXIS_BOTH)
			? VIEW_AXIS_NONE : VIEW_AXIS_BOTH);
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
		view_apply_special_geom(view->id);
	}
}

void
view_toggle_fullscreen(struct view *view)
{
	assert(view);

	view_fullscreen(view->id, !view->st->fullscreen);
}

void
view_notify_fullscreen(struct view *view)
{
	if (view->st->ssd_enabled) {
		if (view->st->fullscreen) {
			/* Hide decorations when going fullscreen */
			undecorate(view);
		} else {
			/* Re-show decorations when no longer fullscreen */
			decorate(view);
		}
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
	if (view == front || view == view_get_root(front->id)) {
		return;
	}

	struct view *root = view_get_root(view->id);
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
	struct view *root = view_get_root(view->id);
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
view_set_visible(struct view *view, bool visible)
{
	wlr_scene_node_set_enabled(&view->scene_tree->node, visible);

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
