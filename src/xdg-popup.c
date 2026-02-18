// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 the sway authors
 *
 * This file is only needed in support of
 *	- unconstraining XDG popups
 *	- keeping non-layer-shell xdg-popups outside the layers.c code
 */

#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "common/macros.h"
#include "common/mem.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "view.h"

struct xdg_popup {
	struct view *parent_view;
	struct wlr_xdg_popup *wlr_popup;

	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *surface_tree;

	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener new_popup;
	struct wl_listener reposition;
};

static void
popup_unconstrain(struct xdg_popup *popup)
{
	struct view *view = popup->parent_view;

	/* Get position of parent toplevel/popup */
	int parent_lx, parent_ly;
	struct wlr_scene_tree *parent_tree = popup->scene_tree->node.parent;
	wlr_scene_node_coords(&parent_tree->node, &parent_lx, &parent_ly);

	/*
	 * Get usable area to constrain by
	 *
	 * The scheduled top-left corner (x, y) of the popup is sometimes less
	 * than zero, typically with Qt apps. We therefore clamp it to avoid for
	 * example the 'File' menu of a maximized window to end up on an another
	 * output.
	 */
	struct wlr_box *popup_box = &popup->wlr_popup->scheduled.geometry;
	struct output *output = output_nearest_to(parent_lx + MAX(popup_box->x, 0),
		parent_ly + MAX(popup_box->y, 0));
	struct wlr_box usable = output_usable_area_in_layout_coords(output);

	/* Get offset of toplevel window from its surface */
	int toplevel_dx = 0;
	int toplevel_dy = 0;
	struct wlr_xdg_surface *toplevel_surface = xdg_surface_from_view(view);
	if (toplevel_surface) {
		toplevel_dx = toplevel_surface->current.geometry.x;
		toplevel_dy = toplevel_surface->current.geometry.y;
	} else {
		wlr_log(WLR_ERROR, "toplevel is not valid XDG surface");
	}

	/* Geometry of usable area relative to toplevel surface */
	struct wlr_box output_toplevel_box = {
		.x = usable.x - (view->current.x - toplevel_dx),
		.y = usable.y - (view->current.y - toplevel_dy),
		.width = usable.width,
		.height = usable.height,
	};
	wlr_xdg_popup_unconstrain_from_box(popup->wlr_popup, &output_toplevel_box);
}

static void
handle_destroy(struct wl_listener *listener, void *data)
{
	struct xdg_popup *popup = wl_container_of(listener, popup, destroy);

	struct wlr_xdg_popup *_popup, *tmp;
	wl_list_for_each_safe(_popup, tmp, &popup->wlr_popup->base->popups, link) {
		wlr_xdg_popup_destroy(_popup);
	}

	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->new_popup.link);
	wl_list_remove(&popup->reposition.link);
	wl_list_remove(&popup->commit.link);

	wlr_scene_node_destroy(&popup->scene_tree->node);

	cursor_update_focus();

	free(popup);
}

static void
handle_commit(struct wl_listener *listener, void *data)
{
	struct xdg_popup *popup = wl_container_of(listener, popup, commit);
	struct wlr_xdg_surface *xdg_surface = popup->wlr_popup->base;

	wlr_scene_node_set_position(&popup->scene_tree->node,
		popup->wlr_popup->current.geometry.x,
		popup->wlr_popup->current.geometry.y);
	wlr_scene_node_set_position(&popup->surface_tree->node,
		-xdg_surface->geometry.x, -xdg_surface->geometry.y);

	if (popup->wlr_popup->base->initial_commit) {
		popup_unconstrain(popup);
	}
}

static void
handle_reposition(struct wl_listener *listener, void *data)
{
	struct xdg_popup *popup = wl_container_of(listener, popup, reposition);
	popup_unconstrain(popup);
}

static void
handle_new_popup(struct wl_listener *listener, void *data)
{
	struct xdg_popup *popup = wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	xdg_popup_create(popup->parent_view, wlr_popup, popup->scene_tree);
}

void
xdg_popup_create(struct view *view, struct wlr_xdg_popup *wlr_popup,
		struct wlr_scene_tree *parent_tree)
{
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(wlr_popup->parent);
	if (!parent) {
		wlr_log(WLR_ERROR, "parent is not a valid XDG surface");
		return;
	}

	struct xdg_popup *popup = znew(*popup);
	popup->parent_view = view;
	popup->wlr_popup = wlr_popup;

	CONNECT_SIGNAL(wlr_popup, popup, destroy);
	CONNECT_SIGNAL(wlr_popup->base, popup, new_popup);
	CONNECT_SIGNAL(wlr_popup->base->surface, popup, commit);
	CONNECT_SIGNAL(wlr_popup, popup, reposition);

	popup->scene_tree = wlr_scene_tree_create(parent_tree);
	popup->surface_tree = wlr_scene_subsurface_tree_create(
		popup->scene_tree, wlr_popup->base->surface);

	node_descriptor_create(&popup->scene_tree->node,
		LAB_NODE_XDG_POPUP, view, /*data*/ NULL);
}
