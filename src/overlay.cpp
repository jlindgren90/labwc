// SPDX-License-Identifier: GPL-2.0-only
#include "overlay.h"
#include <assert.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include "common/lab-scene-rect.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "output.h"
#include "regions.h"
#include "theme.h"
#include "view.h"

static void
create_overlay_rect(struct overlay_rect *rect,
		struct theme_snapping_overlay *theme)
{
	rect->bg_enabled = theme->bg_enabled;
	rect->border_enabled = theme->border_enabled;
	rect->tree = wlr_scene_tree_create(&g_server.scene->tree);

	if (rect->bg_enabled) {
		/* Create a filled rectangle */
		rect->bg_rect = wlr_scene_rect_create(
			rect->tree, 0, 0, theme->bg_color);
	}

	if (rect->border_enabled) {
		/* Create outlines */
		struct lab_scene_rect_options opts = {
			.border_colors = (float *[3]) {
				theme->border_color[0],
				theme->border_color[1],
				theme->border_color[2],
			},
			.nr_borders = 3,
			.border_width = theme->border_width,
		};
		rect->border_rect = lab_scene_rect_create(rect->tree, &opts);
	}

	wlr_scene_node_set_enabled(&rect->tree->node, false);
}

void
overlay_reconfigure(void)
{
	if (g_seat.overlay.region_rect.tree) {
		wlr_scene_node_destroy(&g_seat.overlay.region_rect.tree->node);
	}
	if (g_seat.overlay.edge_rect.tree) {
		wlr_scene_node_destroy(&g_seat.overlay.edge_rect.tree->node);
	}

	create_overlay_rect(&g_seat.overlay.region_rect,
		&g_theme.snapping_overlay_region);
	create_overlay_rect(&g_seat.overlay.edge_rect,
		&g_theme.snapping_overlay_edge);
}

static void
show_overlay(struct overlay_rect *rect, struct wlr_box *box)
{
	struct view *view = g_server.grabbed_view;
	assert(view);

	if (!rect->tree) {
		overlay_reconfigure();
		assert(rect->tree);
	}

	if (rect->bg_enabled) {
		wlr_scene_rect_set_size(rect->bg_rect, box->width, box->height);
	}
	if (rect->border_enabled) {
		lab_scene_rect_set_size(rect->border_rect, box->width, box->height);
	}

	struct wlr_scene_node *node = &rect->tree->node;
	wlr_scene_node_reparent(node, view->scene_tree->node.parent);
	wlr_scene_node_place_below(node, &view->scene_tree->node);
	wlr_scene_node_set_position(node, box->x, box->y);
	wlr_scene_node_set_enabled(node, true);
}

static void
inactivate_overlay(struct overlay *overlay)
{
	if (overlay->region_rect.tree) {
		wlr_scene_node_set_enabled(
			&overlay->region_rect.tree->node, false);
	}
	if (overlay->edge_rect.tree) {
		wlr_scene_node_set_enabled(
			&overlay->edge_rect.tree->node, false);
	}
	overlay->active.region.reset();
	overlay->active.edge = LAB_EDGE_NONE;
	overlay->active.output = NULL;
	if (overlay->timer) {
		wl_event_source_timer_update(overlay->timer, 0);
	}
}

static void
show_region_overlay(struct region *region)
{
	if (g_seat.overlay.active.region == region) {
		return;
	}
	inactivate_overlay(&g_seat.overlay);
	g_seat.overlay.active.region = weakptr(region);

	show_overlay(&g_seat.overlay.region_rect, &region->geo);
}

/* TODO: share logic with view_get_edge_snap_box() */
static struct wlr_box get_edge_snap_box(enum lab_edge edge, struct output *output)
{
	struct wlr_box box = output_usable_area_in_layout_coords(output);
	switch (edge) {
	case LAB_EDGE_RIGHT:
		box.x += box.width / 2;
		/* fallthrough */
	case LAB_EDGE_LEFT:
		box.width /= 2;
		break;
	case LAB_EDGE_BOTTOM:
		box.y += box.height / 2;
		/* fallthrough */
	case LAB_EDGE_TOP:
		box.height /= 2;
		break;
	case LAB_EDGE_CENTER:
		/* <topMaximize> */
		break;
	default:
		/* not reached */
		assert(false);
	}
	return box;
}

static int
handle_edge_overlay_timeout(void *data)
{
	assert(g_seat.overlay.active.edge != LAB_EDGE_NONE
		&& g_seat.overlay.active.output);
	struct wlr_box box = get_edge_snap_box(g_seat.overlay.active.edge,
		g_seat.overlay.active.output);
	show_overlay(&g_seat.overlay.edge_rect, &box);
	return 0;
}

static bool
edge_has_adjacent_output_from_cursor(struct output *output, enum lab_edge edge)
{
	/* Allow only up/down/left/right */
	if (!lab_edge_is_cardinal(edge)) {
		return false;
	}
	/* Cast from enum lab_edge to enum wlr_direction is safe */
	return wlr_output_layout_adjacent_output(g_server.output_layout,
		(enum wlr_direction)edge, output->wlr_output, g_seat.cursor->x,
		g_seat.cursor->y);
}

static void
show_edge_overlay(enum lab_edge edge, struct output *output)
{
	if (!rc.snap_overlay_enabled) {
		return;
	}
	if (g_seat.overlay.active.edge == edge
			&& g_seat.overlay.active.output == output) {
		return;
	}
	inactivate_overlay(&g_seat.overlay);
	g_seat.overlay.active.edge = edge;
	g_seat.overlay.active.output = output;

	int delay;
	if (edge_has_adjacent_output_from_cursor(output, edge)) {
		delay = rc.snap_overlay_delay_inner;
	} else {
		delay = rc.snap_overlay_delay_outer;
	}

	if (delay > 0) {
		if (!g_seat.overlay.timer) {
			g_seat.overlay.timer =
				wl_event_loop_add_timer(g_server.wl_event_loop,
					handle_edge_overlay_timeout, NULL);
		}
		/* Show overlay <snapping><preview><delay>ms later */
		wl_event_source_timer_update(g_seat.overlay.timer, delay);
	} else {
		/* Show overlay now */
		struct wlr_box box =
			get_edge_snap_box(g_seat.overlay.active.edge,
				g_seat.overlay.active.output);
		show_overlay(&g_seat.overlay.edge_rect, &box);
	}
}

void
overlay_update(void)
{
	/* Region-snapping overlay */
	if (regions_should_snap()) {
		auto region = regions_from_cursor();
		if (region) {
			show_region_overlay(region.get());
			return;
		}
	}

	/* Edge-snapping overlay */
	struct output *output;
	enum lab_edge edge = edge_from_cursor(&output);
	if (edge != LAB_EDGE_NONE) {
		show_edge_overlay(edge, output);
		return;
	}

	overlay_hide();
}

void
overlay_hide(void)
{
	struct overlay *overlay = &g_seat.overlay;

	inactivate_overlay(overlay);

	/*
	 * Reparent the rectangle nodes to server's scene-tree so they don't
	 * get destroyed on view destruction
	 */
	if (overlay->region_rect.tree) {
		wlr_scene_node_reparent(&overlay->region_rect.tree->node,
			&g_server.scene->tree);
	}
	if (overlay->edge_rect.tree) {
		wlr_scene_node_reparent(&overlay->edge_rect.tree->node,
			&g_server.scene->tree);
	}
}

void
overlay_finish(void)
{
	if (g_seat.overlay.timer) {
		wl_event_source_remove(g_seat.overlay.timer);
		g_seat.overlay.timer = NULL;
	}
}
