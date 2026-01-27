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
#include "cycle.h"
#include "labwc.h"
#include "menu/menu.h"
#include "session-lock.h"
#include "ssd.h"

#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
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

void
view_notify_active(struct view *view)
{
	ssd_set_active(view->ssd, view->st->active);
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
		view_maximize(view->id, (view->st->maximized == VIEW_AXIS_BOTH) ?
			VIEW_AXIS_NONE : VIEW_AXIS_BOTH);
		break;
	default:
		break;
	}
}

bool
view_is_always_on_top(struct view *view)
{
	assert(view);
	return view->scene_tree->node.parent
		== g_server.view_tree_always_on_top;
}

void
view_toggle_always_on_top(struct view *view)
{
	assert(view);
	if (view_is_always_on_top(view)) {
		wlr_scene_node_reparent(&view->scene_tree->node,
			g_server.view_tree);
	} else {
		wlr_scene_node_reparent(&view->scene_tree->node,
			g_server.view_tree_always_on_top);
	}
}

static void
decorate(struct view *view)
{
	if (!view->ssd) {
		view->ssd = ssd_create(view, view->st->active);
	}
}

static void
undecorate(struct view *view)
{
	ssd_destroy(view->ssd);
	view->ssd = NULL;
}

void
view_set_ssd_enabled(struct view *view, bool enabled)
{
	assert(view);

	if (view->st->fullscreen || enabled == view->ssd_enabled) {
		return;
	}

	/*
	 * Set these first since they are referenced
	 * within the call tree of ssd_create() and ssd_get_margin()
	 */
	view->ssd_enabled = enabled;

	if (enabled) {
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
	if (view->ssd_enabled) {
		if (view->st->fullscreen) {
			/* Hide decorations when going fullscreen */
			undecorate(view);
		} else {
			/* Re-show decorations when no longer fullscreen */
			decorate(view);
		}
	}

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

void
view_raise_impl(struct view *view)
{
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
}

bool
view_focus_impl(struct view *view)
{
	struct wlr_surface *surface = view ? view->surface : NULL;

	if (g_seat.seat->keyboard_state.focused_surface != surface) {
		seat_focus_surface_no_notify(surface);
	}

	return g_seat.seat->keyboard_state.focused_surface == surface;
}

void
view_notify_title_change(struct view *view)
{
	ssd_update_title(view->ssd);
}

void
view_notify_icon_change(struct view *view)
{
	view_drop_icon_buffer(view->id);
	ssd_update_icon(view->ssd);
}

void
view_reload_ssd(struct view *view)
{
	assert(view);
	view_drop_icon_buffer(view->id);

	if (view->ssd_enabled && !view->st->fullscreen) {
		undecorate(view);
		decorate(view);
	}
}

void
view_toggle_keybinds(struct view *view)
{
	assert(view);
	view->inhibits_keybinds = !view->inhibits_keybinds;

	if (view->ssd_enabled) {
		ssd_enable_keybind_inhibit_indicator(view->ssd,
			view->inhibits_keybinds);
	}
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
view_set_visible(struct view *view, bool visible)
{
	wlr_scene_node_set_enabled(&view->scene_tree->node, visible);

	/* View might have been unmapped/minimized during move/resize */
	if (!visible) {
		interactive_cancel(view);
	}
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

	cursor_on_view_destroy(view);

	/*
	 * This check is (in theory) redundant since interactive_cancel()
	 * is called at unmap. Leaving it here just to be sure.
	 */
	if (g_server.grabbed_view == view) {
		interactive_cancel(view);
	}

	if (g_server.session_lock_manager->last_active_view == view) {
		g_server.session_lock_manager->last_active_view = NULL;
	}

	undecorate(view);
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
	free(view);

	cursor_update_focus();
}
