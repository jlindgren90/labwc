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
#include "xwayland/server.h"
#include "xwayland/xwayland.h"
#include "xwayland/xwm.h"

struct wlr_surface *
xwayland_view_get_surface(struct xwayland_surface *xsurface)
{
	return xsurface->surface;
}

void
xwayland_surface_on_commit(struct xwayland_surface *xsurface)
{
	const ViewState *view_st = view_get_state(xsurface->view_id);
	if (!view_st || !xsurface->surface || !xsurface->surface->mapped) {
		return;
	}

	/* Must receive commit signal before accessing surface->current* */
	struct wlr_surface_state *state = &xsurface->surface->current;

	/*
	 * If there is a pending move/resize, wait until the surface
	 * size changes to update geometry. The hope is to update both
	 * the position and the size of the view at the same time,
	 * reducing visual glitches.
	 */
	if (view_st->current.width != state->width
			|| view_st->current.height != state->height) {
		view_commit_geom(xsurface->view_id, state->width, state->height);
	}
}

void
xwayland_surface_on_request_move(struct xwayland_surface *xsurface)
{
	/*
	 * This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check
	 * the provided serial against a list of button press serials sent to
	 * this client, to prevent the client from requesting this whenever they
	 * want.
	 *
	 * Note: interactive_begin() checks that view == server.grabbed_view.
	 */
	interactive_begin(xsurface->view_id, LAB_INPUT_STATE_MOVE, LAB_EDGE_NONE);
}

void
xwayland_surface_on_request_resize(struct xwayland_surface *xsurface, uint32_t edges)
{
	/*
	 * This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check
	 * the provided serial against a list of button press serials sent to
	 * this client, to prevent the client from requesting this whenever they
	 * want.
	 *
	 * Note: interactive_begin() checks that view == server.grabbed_view.
	 */
	interactive_begin(xsurface->view_id, LAB_INPUT_STATE_RESIZE, edges);
}

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
xwayland_surface_on_request_configure(struct xwayland_surface *xsurface,
		const struct xwayland_surface_configure_event *event)
{
	if (xsurface->override_redirect) {
		assert(!xsurface->view_id);
		xwayland_surface_configure(xsurface, event->geom);
		if (xsurface->unmanaged_node) {
			wlr_scene_node_set_position(xsurface->unmanaged_node,
				event->geom.x, event->geom.y);
			cursor_update_focus();
		}
		return;
	}

	const ViewState *view_st = view_get_state(xsurface->view_id);
	if (!view_st) {
		return;
	}

	if (view_is_floating(view_st)) {
		/* Honor client configure requests for floating views */
		struct wlr_box box = event->geom;
		view_adjust_size(xsurface->view_id, &box.width, &box.height);
		view_move_resize(xsurface->view_id, box);
	} else {
		/*
		 * Do not allow clients to request geometry other than
		 * what we computed for maximized/fullscreen/tiled
		 * views. Ignore the client request and send back a
		 * ConfigureNotify event with the computed geometry.
		 */
		xwayland_surface_configure(xsurface, view_st->pending);
	}
}

void
xwayland_surface_on_request_activate(struct xwayland_surface *xsurface)
{
	if (xsurface->override_redirect) {
		assert(!xsurface->view_id);
		if (xsurface->surface && xsurface->surface->mapped) {
			seat_focus_surface(xsurface->surface);
		}
	} else {
		view_focus(xsurface->view_id, /* raise */ true);
	}
}

void
xwayland_surface_on_set_override_redirect(struct xwayland_surface *xsurface)
{
	if (xsurface->surface && xsurface->surface->mapped) {
		xwayland_surface_on_unmap(xsurface);
	}

	if (xsurface->override_redirect) {
		view_remove(xsurface->view_id);
	} else {
		assert(!xsurface->view_id);
		view_add_xwayland(xsurface->window_id, xsurface);
		// Re-read properties after unmanaged surface becomes managed
		xwayland_surface_read_properties(xsurface);
	}

	if (xsurface->surface && xsurface->surface->mapped) {
		xwayland_surface_on_map(xsurface);
	}
}

void
xwayland_surface_on_set_icon(struct xwayland_surface *xsurface)
{
	view_clear_icon_surfaces(xsurface->view_id);

	xcb_ewmh_get_wm_icon_reply_t icon_reply = {0};
	if (!xwayland_surface_fetch_icon(xsurface, &icon_reply)) {
		wlr_log(WLR_INFO, "Invalid x11 icon");
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
		view_add_icon_surface(xsurface->view_id, surface);
	}

out:
	view_update_icon(xsurface->view_id);
	xcb_ewmh_get_wm_icon_reply_wipe(&icon_reply);
}

void
xwayland_surface_on_focus_in(struct xwayland_surface *xsurface)
{
	struct wlr_surface *surface = xsurface->surface;

	if (!surface || !surface->mapped) {
		/*
		 * It is rare but possible for the focus_in event to be
		 * received before the map event. This has been seen
		 * during CLion startup, when focus is initially offered
		 * to the splash screen but accepted later by the main
		 * window instead. (In this case, the focus transfer is
		 * client-initiated but allowed by wlroots because the
		 * same PID owns both windows.)
		 */
		return;
	}

	if (surface != g_seat.wlr_seat->keyboard_state.focused_surface) {
		seat_focus_surface(surface);
	}
}

static void
map_unmanaged_surface(struct xwayland_surface *xsurface)
{
	assert(!xsurface->view_id);
	assert(!xsurface->unmanaged_node);

	if (xsurface->ever_grabbed_focus) {
		seat_focus_surface(xsurface->surface);
	}

	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_create(server.unmanaged_tree,
			xsurface->surface);
	die_if_null(scene_surface);

	xsurface->unmanaged_node = &scene_surface->buffer->node;
	wlr_scene_node_set_position(xsurface->unmanaged_node,
		xsurface->props.geom.x, xsurface->props.geom.y);
	cursor_update_focus();
}

void
xwayland_surface_on_map(struct xwayland_surface *xsurface)
{
	assert(xsurface->surface);

	if (xsurface->override_redirect) {
		map_unmanaged_surface(xsurface);
		return;
	}

	view_map(xsurface->view_id);
}

static void
unmap_unmanaged_surface(struct xwayland_surface *xsurface)
{
	assert(!xsurface->view_id);
	assert(xsurface->unmanaged_node);

	wlr_scene_node_destroy(xsurface->unmanaged_node);
	xsurface->unmanaged_node = NULL;
	cursor_update_focus();

	if (g_seat.wlr_seat->keyboard_state.focused_surface == xsurface->surface) {
		view_refocus_active();
	}
}

void
xwayland_surface_on_unmap(struct xwayland_surface *xsurface)
{
	assert(xsurface->surface);

	if (xsurface->unmanaged_node) {
		unmap_unmanaged_surface(xsurface);
		return;
	}

	view_unmap(xsurface->view_id);
}

void
xwayland_surface_on_grab_focus(struct xwayland_surface *xsurface)
{
	if (xsurface->override_redirect) {
		assert(!xsurface->view_id);
		xsurface->ever_grabbed_focus = true;
		if (xsurface->surface && xsurface->surface->mapped) {
			seat_focus_surface(xsurface->surface);
		}
	}
}

/* for unmanaged surface only */
void
xwayland_surface_on_set_geometry(struct xwayland_surface *xsurface)
{
	if (xsurface->unmanaged_node) {
		wlr_scene_node_set_position(xsurface->unmanaged_node,
			xsurface->props.geom.x, xsurface->props.geom.y);
		cursor_update_focus();
	}
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
		struct wlr_buffer *cursor_buffer = wlr_xcursor_image_get_buffer(image);
		if (cursor_buffer) {
			xwayland_set_cursor(cursor_buffer,
				image->hotspot_x, image->hotspot_y);
		}
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
	/*
	 * Do nothing if called during destroy or before xwayland is ready.
	 * This function will be called again from the ready signal handler.
	 */
	if (!g_xserver.xwm) {
		return;
	}

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
