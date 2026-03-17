// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "xwayland.h"
#include <assert.h>
#include <cairo.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "common/macros.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "config/session.h"
#include "labwc.h"
#include "output.h"
#include "xwayland/xwayland.h"

void
xwayland_view_configure(struct xwayland_surface *xsurface,
		struct wlr_box current, struct wlr_box geo, bool *commit_move)
{
	xwayland_surface_configure(xsurface, geo);

	/*
	 * For unknown reasons, XWayland surfaces that are completely
	 * offscreen seem not to generate commit events. In rare cases,
	 * this can prevent an offscreen window from moving onscreen
	 * (since we wait for a commit event that never occurs). As a
	 * workaround, move offscreen surfaces immediately.
	 */
	bool is_offscreen = !wlr_box_empty(&current)
		&& !wlr_output_layout_intersects(server.output_layout, NULL,
			&current);

	/* If not resizing, process the move immediately */
	if (is_offscreen || (current.width == geo.width
			&& current.height == geo.height)) {
		*commit_move = true;
	}
}

void
xwayland_on_set_window_icon(xcb_window_t window_id)
{
	ViewId view_id = xsurface_get_view_id(window_id);
	if (!view_id) {
		return;
	}
	view_clear_icon_surfaces(view_id);

	xcb_ewmh_get_wm_icon_reply_t icon_reply = {0};
	if (!xwayland_fetch_window_icon(window_id, &icon_reply)) {
		goto out;
	}

	xcb_ewmh_wm_icon_iterator_t iter = xcb_ewmh_get_wm_icon_iterator(&icon_reply);
	for (; iter.rem; xcb_ewmh_get_wm_icon_next(&iter)) {
		cairo_surface_t *surface =
			cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
				iter.width, iter.height);
		uint8_t *dst = cairo_image_surface_get_data(surface);
		int dst_stride = cairo_format_stride_for_width(
			CAIRO_FORMAT_ARGB32, iter.width);

		/* Pre-multiply alpha */
		for (uint32_t y = 0; y < iter.height; y++) {
			for (uint32_t x = 0; x < iter.width; x++) {
				uint32_t i = x + y * iter.width;
				uint8_t *src_pixel = (uint8_t *)&iter.data[i];
				uint8_t *dst_pixel = &dst[x * 4 + y * dst_stride];
				dst_pixel[0] = src_pixel[0] * src_pixel[3] / 255;
				dst_pixel[1] = src_pixel[1] * src_pixel[3] / 255;
				dst_pixel[2] = src_pixel[2] * src_pixel[3] / 255;
				dst_pixel[3] = src_pixel[3];
			}
		}

		cairo_surface_mark_dirty(surface);
		view_add_icon_surface(view_id, surface);
	}

out:
	view_update_icon(view_id);
	xcb_ewmh_get_wm_icon_reply_wipe(&icon_reply);
}

void
xwayland_on_ready(void)
{
	xwayland_update_workarea();

	struct wlr_xcursor *xcursor;
	xcursor = wlr_xcursor_manager_get_xcursor(
		g_seat.xcursor_manager, XCURSOR_DEFAULT, 1);
	if (xcursor) {
		struct wlr_xcursor_image *image = xcursor->images[0];
		xwayland_set_cursor(image->buffer,
			image->width * 4, image->width, image->height,
			image->hotspot_x, image->hotspot_y);
	}

	/* Fire an Xwayland startup script if one (or many) can be found */
	session_run_script("xinitrc");
}

static bool
intervals_overlap(int start_a, int end_a, int start_b, int end_b)
{
	/* check for empty intervals */
	if (end_a <= start_a || end_b <= start_b) {
		return false;
	}

	return start_a < start_b ?
		start_b < end_a :  /* B starts within A */
		start_a < end_b;   /* A starts within B */
}

/*
 * Subtract the area of an XWayland view (e.g. panel) from the usable
 * area of the output based on _NET_WM_STRUT_PARTIAL property.
 */
void
output_adjust_usable_area_for_strut_partial(struct output *output,
		const xcb_ewmh_wm_strut_partial_t *strut)
{
	assert(output);
	assert(strut);

	/* these are layout coordinates */
	struct wlr_box lb = { 0 };
	wlr_output_layout_get_box(server.output_layout, NULL, &lb);
	struct wlr_box ob = { 0 };
	wlr_output_layout_get_box(server.output_layout, output->wlr_output, &ob);

	/*
	 * strut->right/bottom are offsets from the lower right corner
	 * of the X11 screen, which should generally correspond with the
	 * lower right corner of the output layout
	 */
	double strut_left = strut->left;
	double strut_right = (lb.x + lb.width) - strut->right;
	double strut_top = strut->top;
	double strut_bottom = (lb.y + lb.height) - strut->bottom;

	/* convert layout to output coordinates */
	wlr_output_layout_output_coords(server.output_layout,
		output->wlr_output, &strut_left, &strut_top);
	wlr_output_layout_output_coords(server.output_layout,
		output->wlr_output, &strut_right, &strut_bottom);

	struct wlr_box *usable = &output->usable_area;
	/* deal with right/bottom rather than width/height */
	int usable_right = usable->x + usable->width;
	int usable_bottom = usable->y + usable->height;

	/* here we mix output and layout coordinates; be careful */
	if (strut_left > usable->x && strut_left < usable_right
			&& intervals_overlap(ob.y, ob.y + ob.height,
			strut->left_start_y, strut->left_end_y + 1)) {
		usable->x = strut_left;
	}
	if (strut_right > usable->x && strut_right < usable_right
			&& intervals_overlap(ob.y, ob.y + ob.height,
			strut->right_start_y, strut->right_end_y + 1)) {
		usable_right = strut_right;
	}
	if (strut_top > usable->y && strut_top < usable_bottom
			&& intervals_overlap(ob.x, ob.x + ob.width,
			strut->top_start_x, strut->top_end_x + 1)) {
		usable->y = strut_top;
	}
	if (strut_bottom > usable->y && strut_bottom < usable_bottom
			&& intervals_overlap(ob.x, ob.x + ob.width,
			strut->bottom_start_x, strut->bottom_end_x + 1)) {
		usable_bottom = strut_bottom;
	}

	usable->width = usable_right - usable->x;
	usable->height = usable_bottom - usable->y;
}

void
xwayland_update_workarea(void)
{
	struct wlr_box lb;
	wlr_output_layout_get_box(server.output_layout, NULL, &lb);

	/* Compute outer edges of layout (excluding negative regions) */
	int layout_left = MAX(0, lb.x);
	int layout_right = MAX(0, lb.x + lb.width);
	int layout_top = MAX(0, lb.y);
	int layout_bottom = MAX(0, lb.y + lb.height);

	/* Workarea is initially the entire layout */
	int workarea_left = layout_left;
	int workarea_right = layout_right;
	int workarea_top = layout_top;
	int workarea_bottom = layout_bottom;

	struct output *output;
	wl_list_for_each(output, &server.outputs, link) {
		if (!output_is_usable(output)) {
			continue;
		}

		struct wlr_box ob;
		wlr_output_layout_get_box(server.output_layout,
			output->wlr_output, &ob);

		/* Compute edges of output */
		int output_left = ob.x;
		int output_right = ob.x + ob.width;
		int output_top = ob.y;
		int output_bottom = ob.y + ob.height;

		/* Compute edges of usable area */
		int usable_left = output_left + output->usable_area.x;
		int usable_right = usable_left + output->usable_area.width;
		int usable_top = output_top + output->usable_area.y;
		int usable_bottom = usable_top + output->usable_area.height;

		/*
		 * Only adjust workarea edges for output edges that are
		 * aligned with outer edges of layout
		 */
		if (output_left == layout_left) {
			workarea_left = MAX(workarea_left, usable_left);
		}
		if (output_right == layout_right) {
			workarea_right = MIN(workarea_right, usable_right);
		}
		if (output_top == layout_top) {
			workarea_top = MAX(workarea_top, usable_top);
		}
		if (output_bottom == layout_bottom) {
			workarea_bottom = MIN(workarea_bottom, usable_bottom);
		}
	}

	/*
	 * Set _NET_WORKAREA property. We don't report virtual desktops
	 * to XWayland, so we set only one workarea.
	 */
	struct wlr_box workarea = {
		.x = workarea_left,
		.y = workarea_top,
		.width = workarea_right - workarea_left,
		.height = workarea_bottom - workarea_top,
	};
	xwayland_set_workareas(&workarea, 1);
}

struct wlr_scene_node *
xwayland_create_unmanaged_node(struct wlr_surface *surface)
{
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_create(server.unmanaged_tree, surface);
	die_if_null(scene_surface);
	return &scene_surface->buffer->node;
}
