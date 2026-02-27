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
#include "node.h"
#include "session-lock.h"

ViewId
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
		return (ViewId)xdg_surface->data;
	}

	struct xwayland_surface *xsurface =
		xwayland_surface_try_from_wlr_surface(surface);
	if (xsurface) {
		return (ViewId)xsurface->data;
	}

	return 0;
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
view_inhibits_actions(ViewId view_id, struct wl_list *actions)
{
	const ViewState *view_st = view_get_state(view_id);
	return view_st && view_st->inhibits_keybinds
		&& !actions_contain_toggle_keybinds(actions);
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

	menu_on_view_destroy(view->id);

	view_remove(view->id);
	free(view);

	cursor_update_focus();
}

struct wlr_scene_tree *
view_scene_tree_create(ViewId id)
{
	struct wlr_scene_tree *scene_tree =
		wlr_scene_tree_create(g_server.view_tree);

	node_descriptor_create(&scene_tree->node,
		LAB_NODE_VIEW, id, /* data */ NULL);
	wlr_scene_node_set_enabled(&scene_tree->node, false);

	return scene_tree;
}

void
view_scene_tree_destroy(struct wlr_scene_tree *scene_tree)
{
	wlr_scene_node_destroy(&scene_tree->node);
}

void
view_scene_tree_move(struct wlr_scene_tree *scene_tree, int x, int y)
{
	wlr_scene_node_set_position(&scene_tree->node, x, y);
}

void
view_scene_tree_raise(struct wlr_scene_tree *scene_tree)
{
	wlr_scene_node_raise_to_top(&scene_tree->node);
}

void
view_scene_tree_set_visible(struct wlr_scene_tree *scene_tree, bool visible)
{
	wlr_scene_node_set_enabled(&scene_tree->node, visible);
}

struct wlr_scene_tree *
view_surface_tree_create(struct view *view, struct wlr_scene_tree *scene_tree)
{
	return wlr_scene_subsurface_tree_create(scene_tree,
		view_get_surface(view));
}

struct wlr_scene_rect *
view_fullscreen_bg_create(struct wlr_scene_tree *scene_tree)
{
	const float black[4] = {0, 0, 0, 1};
	struct wlr_scene_rect *fullscreen_bg =
		wlr_scene_rect_create(scene_tree, 0, 0, black);
	wlr_scene_node_lower_to_bottom(&fullscreen_bg->node);
	return fullscreen_bg;
}

void
view_fullscreen_bg_show_at(struct wlr_scene_rect *fullscreen_bg,
		struct wlr_box rel_geom)
{
	wlr_scene_node_set_position(&fullscreen_bg->node, rel_geom.x, rel_geom.y);
	wlr_scene_rect_set_size(fullscreen_bg, rel_geom.width, rel_geom.height);
	wlr_scene_node_set_enabled(&fullscreen_bg->node, true);
}

void
view_fullscreen_bg_hide(struct wlr_scene_rect *fullscreen_bg)
{
	wlr_scene_node_set_enabled(&fullscreen_bg->node, false);
}
