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
#include "cycle.h"
#include "labwc.h"
#include "menu/menu.h"
#include "session-lock.h"

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
view_move_impl(struct view *view)
{
	assert(view);
	wlr_scene_node_set_position(&view->scene_tree->node,
		view->st->current.x, view->st->current.y);
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

void
view_toggle_fullscreen(struct view *view)
{
	assert(view);

	view_fullscreen(view->id, !view->st->fullscreen);
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
	struct wlr_surface *surface = view ? view_get_surface(view) : NULL;

	if (g_seat.wlr_seat->keyboard_state.focused_surface != surface) {
		seat_focus_surface_no_notify(surface);
	}

	return g_seat.wlr_seat->keyboard_state.focused_surface == surface;
}

bool
view_inhibits_actions(struct view *view, struct wl_list *actions)
{
	return view && view->st->inhibits_keybinds
		&& !actions_contain_toggle_keybinds(actions);
}

/* Used in both (un)map and (un)minimize */
void
view_set_visible(struct view *view, bool visible)
{
	wlr_scene_node_set_enabled(&view->scene_tree->node, visible);
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

	/* Must come before destroying view->scene_tree */
	view_destroy_ssd(view->id);
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
