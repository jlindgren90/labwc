// SPDX-License-Identifier: GPL-2.0-only

#define _POSIX_C_SOURCE 200809L
#include "regions.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <wlr/types/wlr_cursor.h>
#include "config/rcxml.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "output.h"
#include "view.h"

bool
regions_should_snap(void)
{
	if (g_server.input_mode != LAB_INPUT_STATE_MOVE
			|| rc.regions.empty()
			|| g_seat.region_prevent_snap
			|| !view_is_floating(g_server.grabbed_view)) {
		return false;
	}

	return keyboard_get_all_modifiers();
}

refptr<region>
regions_from_name(const char *region_name, struct output *output)
{
	assert(region_name);
	assert(output);
	for (auto &region : output->regions) {
		if (region.name == region_name) {
			return refptr(&region);
		}
	}
	return {};
}

refptr<region>
regions_from_cursor(void)
{
	double lx = g_seat.cursor->x;
	double ly = g_seat.cursor->y;

	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(g_server.output_layout, lx, ly);
	struct output *output = output_from_wlr_output(wlr_output);
	if (!output) {
		return {};
	}

	double dist;
	double dist_min = DBL_MAX;
	refptr<region> closest_region;
	for (auto &region : output->regions) {
		if (wlr_box_contains_point(&region.geo, lx, ly)) {
			/* No need for sqrt((x1 - x2)^2 + (y1 - y2)^2) as we just compare */
			dist = pow(region.center.x - lx, 2)
				+ pow(region.center.y - ly, 2);
			if (dist < dist_min) {
				closest_region.reset(&region);
				dist_min = dist;
			}
		}
	}
	return closest_region;
}

void
regions_reconfigure_output(struct output *output)
{
	assert(output);

	/* Evacuate views and destroy current regions */
	if (!output->regions.empty()) {
		regions_evacuate_output(output);
		output->regions.clear();
	}

	/* Initialize regions from config */
	for (auto &region : rc.regions) {
		/* Create a copy */
		output->regions.append(new ::region{
			.output = output,
			.name = region.name,
			.percentage = region.percentage,
		});
	}

	/* Update region geometries */
	regions_update_geometry(output);
}

void
regions_reconfigure(void)
{
	/* Evacuate views and initialize regions from config */
	for (auto &output : g_server.outputs) {
		regions_reconfigure_output(&output);
	}

	/* Tries to match the evacuated views to the new regions */
	desktop_arrange_all_views();
}

void
regions_update_geometry(struct output *output)
{
	assert(output);

	struct wlr_box usable = output_usable_area_in_layout_coords(output);

	/* Update regions */
	struct wlr_box *perc, *geo;
	for (auto &region : output->regions) {
		geo = &region.geo;
		perc = &region.percentage;
		/*
		 * Add percentages (x + width, y + height) before scaling
		 * so that there is no gap between regions due to rounding
		 * variations
		 */
		int left = usable.width * perc->x / 100;
		int right = usable.width * (perc->x + perc->width) / 100;
		int top = usable.height * perc->y / 100;
		int bottom = usable.height * (perc->y + perc->height) / 100;
		geo->x = usable.x + left;
		geo->y = usable.y + top;
		geo->width = right - left;
		geo->height = bottom - top;
		region.center.x = geo->x + geo->width / 2;
		region.center.y = geo->y + geo->height / 2;
	}
}

void
regions_evacuate_output(struct output *output)
{
	assert(output);
	for (auto &view : g_views) {
		if (CHECK_PTR(view.tiled_region, region)
				&& region->output == output) {
			view_evacuate_region(&view);
		}
	}
}

void
regions_destroy(reflist<region> &regions)
{
	for (auto &region : regions) {
		if (g_seat.overlay.active.region == &region) {
			overlay_hide();
		}
	}
	regions.clear();
}

