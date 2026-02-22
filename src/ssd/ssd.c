// SPDX-License-Identifier: GPL-2.0-only

/*
 * Helpers for view server side decorations
 *
 * Copyright (C) Johan Malm 2020-2021
 */

#include "ssd.h"
#include <assert.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include "common/mem.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

/*
 * Space between the extremities of the view's wlr_surface
 * and the max extents of the server-side decorations.
 * For xdg-shell views with CSD, this margin is zero.
 */
struct border
ssd_get_margin(const ViewState *view_st)
{
	/*
	 * Check preconditions for displaying SSD. Note that this
	 * needs to work even before ssd_create() has been called.
	 *
	 * For that reason we are not using the .enabled state of
	 * the titlebar node here but rather check for the view
	 * boolean. If we were to use the .enabled state this would
	 * cause issues on Reconfigure events with views which were
	 * in border-only deco mode as view->ssd would only be set
	 * after ssd_create() returns.
	 */
	if (!view_st || !view_st->ssd_enabled || view_st->fullscreen) {
		return (struct border){ 0 };
	}

	if (view_st->maximized == VIEW_AXIS_BOTH) {
		return (struct border){
			.top = g_theme.titlebar_height,
		};
	}

	return (struct border){
		.top = g_theme.titlebar_height + BORDER_PX_TOP,
		.right = BORDER_PX_SIDE,
		.bottom = BORDER_PX_SIDE,
		.left = BORDER_PX_SIDE,
	};
}

struct wlr_box
ssd_max_extents(const ViewState *view_st)
{
	if (!view_st) {
		return (struct wlr_box){0};
	}
	struct border border = ssd_get_margin(view_st);

	return (struct wlr_box){
		.x = view_st->current.x - border.left,
		.y = view_st->current.y - border.top,
		.width = view_st->current.width + border.left + border.right,
		.height = view_st->current.height + border.top + border.bottom,
	};
}

/*
 * Resizing and mouse contexts like 'Left', 'TLCorner', etc. in the vicinity of
 * SSD borders, titlebars and extents can have effective "corner regions" that
 * behave differently from single-edge contexts.
 *
 * Corner regions are active whenever the cursor is within a prescribed size
 * (generally rc.resize_corner_range, but clipped to view size) of the view
 * bounds, so check the cursor against the view here.
 */
enum lab_node_type
ssd_get_resizing_type(const ViewState *view_st, struct wlr_cursor *cursor)
{
	if (!view_st || !cursor || !view_st->ssd_enabled || view_st->fullscreen) {
		return LAB_NODE_NONE;
	}

	struct wlr_box view_box = view_st->current;

	/* Consider the titlebar part of the view */
	int titlebar_height = g_theme.titlebar_height;
	view_box.y -= titlebar_height;
	view_box.height += titlebar_height;

	if (wlr_box_contains_point(&view_box, cursor->x, cursor->y)) {
		/* A cursor in bounds of the view is never in an SSD context */
		return LAB_NODE_NONE;
	}

	int corner_height = MAX(0, MIN(rc.resize_corner_range, view_box.height / 2));
	int corner_width = MAX(0, MIN(rc.resize_corner_range, view_box.width / 2));
	bool left = cursor->x < view_box.x + corner_width;
	bool right = cursor->x > view_box.x + view_box.width - corner_width;
	bool top = cursor->y < view_box.y + corner_height;
	bool bottom = cursor->y > view_box.y + view_box.height - corner_height;

	if (top && left) {
		return LAB_NODE_CORNER_TOP_LEFT;
	} else if (top && right) {
		return LAB_NODE_CORNER_TOP_RIGHT;
	} else if (bottom && left) {
		return LAB_NODE_CORNER_BOTTOM_LEFT;
	} else if (bottom && right) {
		return LAB_NODE_CORNER_BOTTOM_RIGHT;
	} else if (top) {
		return LAB_NODE_BORDER_TOP;
	} else if (bottom) {
		return LAB_NODE_BORDER_BOTTOM;
	} else if (left) {
		return LAB_NODE_BORDER_LEFT;
	} else if (right) {
		return LAB_NODE_BORDER_RIGHT;
	}

	return LAB_NODE_NONE;
}

struct ssd *
ssd_create(struct view *view, struct wlr_buffer *icon_buffer)
{
	assert(view);
	struct ssd *ssd = znew(*ssd);

	ssd->view_id = view->id;
	ssd->tree = wlr_scene_tree_create(view->scene_tree);

	/*
	 * Attach node_descriptor to the root node so that get_cursor_context()
	 * detect cursor hovering on borders and extents.
	 */
	node_descriptor_create(&ssd->tree->node,
		LAB_NODE_SSD_ROOT, view->id, /*data*/ NULL);

	wlr_scene_node_lower_to_bottom(&ssd->tree->node);
	ssd->titlebar.height = g_theme.titlebar_height;
	/*
	 * We need to create the borders after the titlebar because it sets
	 * ssd->state.squared which ssd_border_create() reacts to.
	 * TODO: Set the state here instead so the order does not matter
	 * anymore.
	 */
	ssd_titlebar_create(ssd, view->st, icon_buffer);
	ssd_border_create(ssd, view->st);
	ssd_set_active(ssd, view->st->active);
	ssd->state.geometry = view->st->current;

	return ssd;
}

void
ssd_update_geometry(struct ssd *ssd, const ViewState *view_st)
{
	if (!ssd) {
		return;
	}

	struct wlr_box cached = ssd->state.geometry;
	struct wlr_box current = view_st->current;

	bool update_area = current.width != cached.width
		|| current.height != cached.height;

	bool maximized = view_st->maximized == VIEW_AXIS_BOTH;
	bool state_changed = ssd->state.was_maximized != maximized;

	if (update_area || state_changed) {
		ssd_titlebar_update(ssd, view_st);
		ssd_border_update(ssd, view_st);
	}

	ssd->state.geometry = current;
}

void
ssd_destroy(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}

	/* Maybe reset hover view */
	if (g_server.hovered_button && node_view_from_node(
			g_server.hovered_button->node) == ssd->view_id) {
		g_server.hovered_button = NULL;
	}

	/* Destroy subcomponents */
	ssd_titlebar_destroy(ssd);
	ssd_border_destroy(ssd);
	wlr_scene_node_destroy(&ssd->tree->node);

	free(ssd);
}

void
ssd_set_active(struct ssd *ssd, bool active)
{
	if (!ssd) {
		return;
	}
	enum ssd_active_state active_state;
	FOR_EACH_ACTIVE_STATE(active_state) {
		wlr_scene_node_set_enabled(
			&ssd->border.subtrees[active_state].tree->node,
			active == active_state);
		wlr_scene_node_set_enabled(
			&ssd->titlebar.subtrees[active_state].tree->node,
			active == active_state);
	}
}
