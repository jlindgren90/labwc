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
show_overlay(struct theme_snapping_overlay *overlay_theme,
		struct wlr_box *box)
{
	struct view *view = g_server.grabbed_view;
	assert(view);
	assert(!g_seat.overlay.rect);

	struct lab_scene_rect_options opts = {
		.width = box->width,
		.height = box->height,
	};
	if (overlay_theme->bg_enabled) {
		/* Create a filled rectangle */
		opts.bg_color = overlay_theme->bg_color;
	}
	float *border_colors[3] = {
		overlay_theme->border_color[0],
		overlay_theme->border_color[1],
		overlay_theme->border_color[2],
	};
	if (overlay_theme->border_enabled) {
		/* Create outlines */
		opts.border_colors = border_colors;
		opts.nr_borders = 3;
		opts.border_width = overlay_theme->border_width;
	}

	g_seat.overlay.rect =
		lab_scene_rect_create(view->scene_tree->node.parent, &opts);

	struct wlr_scene_node *node = &g_seat.overlay.rect->tree->node;
	wlr_scene_node_place_below(node, &view->scene_tree->node);
	wlr_scene_node_set_position(node, box->x, box->y);
}

static void
show_region_overlay(struct region *region)
{
	if (region == g_seat.overlay.active.region) {
		return;
	}
	overlay_finish();
	g_seat.overlay.active.region = region;

	struct wlr_box geo = view_get_region_snap_box(NULL, region);
	show_overlay(&g_theme.snapping_overlay_region, &geo);
}

static struct wlr_box
get_edge_snap_box(enum lab_edge edge, struct output *output)
{
	if (edge == LAB_EDGE_TOP && rc.snap_top_maximize) {
		return output_usable_area_in_layout_coords(output);
	} else {
		return view_get_edge_snap_box(NULL, output, edge);
	}
}

static int
handle_edge_overlay_timeout(void *data)
{
	assert(g_seat.overlay.active.edge != LAB_EDGE_NONE
		&& g_seat.overlay.active.output);
	struct wlr_box box = get_edge_snap_box(g_seat.overlay.active.edge,
		g_seat.overlay.active.output);
	show_overlay(&g_theme.snapping_overlay_edge, &box);
	return 0;
}

static bool
edge_has_adjacent_output_from_cursor(struct output *output,
		enum lab_edge edge)
{
	/* Allow only up/down/left/right */
	if (!lab_edge_is_cardinal(edge)) {
		return false;
	}
	/* Cast from enum lab_edge to enum wlr_direction is safe */
	return wlr_output_layout_adjacent_output(
		g_server.output_layout, (enum wlr_direction)edge,
		output->wlr_output, g_seat.cursor->x, g_seat.cursor->y);
}

static void
show_edge_overlay(enum lab_edge edge1, enum lab_edge edge2,
		struct output *output)
{
	if (!rc.snap_overlay_enabled) {
		return;
	}
	enum lab_edge edge = edge1 | edge2;
	if (g_seat.overlay.active.edge == edge
			&& g_seat.overlay.active.output == output) {
		return;
	}
	overlay_finish();
	g_seat.overlay.active.edge = edge;
	g_seat.overlay.active.output = output;

	int delay;
	if (edge_has_adjacent_output_from_cursor(output, edge1)) {
		delay = rc.snap_overlay_delay_inner;
	} else {
		delay = rc.snap_overlay_delay_outer;
	}

	if (delay > 0) {
		if (!g_seat.overlay.timer) {
			g_seat.overlay.timer = wl_event_loop_add_timer(
				g_server.wl_event_loop,
				handle_edge_overlay_timeout, NULL);
		}
		/* Show overlay <snapping><preview><delay>ms later */
		wl_event_source_timer_update(g_seat.overlay.timer, delay);
	} else {
		/* Show overlay now */
		handle_edge_overlay_timeout(NULL);
	}
}

void
overlay_update(void)
{
	/* Region-snapping overlay */
	if (regions_should_snap()) {
		struct region *region = regions_from_cursor();
		if (region) {
			show_region_overlay(region);
			return;
		}
	}

	/* Edge-snapping overlay */
	struct output *output;
	enum lab_edge edge1, edge2;
	if (edge_from_cursor(&output, &edge1, &edge2)) {
		show_edge_overlay(edge1, edge2, output);
		return;
	}

	overlay_finish();
}

void
overlay_finish(void)
{
	if (g_seat.overlay.rect) {
		wlr_scene_node_destroy(&g_seat.overlay.rect->tree->node);
	}
	if (g_seat.overlay.timer) {
		wl_event_source_remove(g_seat.overlay.timer);
	}
	g_seat.overlay = (struct overlay){0};
}
