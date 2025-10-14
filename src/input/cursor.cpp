// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "input/cursor.h"
#include <assert.h>
#include <time.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/region.h>
#include "action.h"
#include "common/macros.h"
#include "config/mousebind.h"
#include "config/rcxml.h"
#include "dnd.h"
#include "idle.h"
#include "input/gestures.h"
#include "input/keyboard.h"
#include "input/tablet.h"
#include "input/touch.h"
#include "labwc.h"
#include "layers.h"
#include "menu/menu.h"
#include "output.h"
#include "resistance.h"
#include "resize-outlines.h"
#include "ssd.h"
#include "view.h"
#include "xwayland.h"

#define LAB_CURSOR_SHAPE_V1_VERSION 1

struct constraint : public destroyable {
	struct wlr_pointer_constraint_v1 *wlr_constraint;
	~constraint();
};

static const char * const *cursor_names = NULL;

/* Usual cursor names */
static const char * const cursors_xdg[] = {
	NULL,
	"default",
	"grab",
	"nw-resize",
	"n-resize",
	"ne-resize",
	"e-resize",
	"se-resize",
	"s-resize",
	"sw-resize",
	"w-resize"
};

/* XCursor fallbacks */
static const char * const cursors_x11[] = {
	NULL,
	"left_ptr",
	"grabbing",
	"top_left_corner",
	"top_side",
	"top_right_corner",
	"right_side",
	"bottom_right_corner",
	"bottom_side",
	"bottom_left_corner",
	"left_side"
};

static_assert(
	ARRAY_SIZE(cursors_xdg) == LAB_CURSOR_COUNT,
	"XDG cursor names are out of sync");
static_assert(
	ARRAY_SIZE(cursors_x11) == LAB_CURSOR_COUNT,
	"X11 cursor names are out of sync");

enum lab_cursors
cursor_get_from_edge(enum lab_edge resize_edges)
{
	switch (resize_edges) {
	case LAB_EDGES_TOP_LEFT:
		return LAB_CURSOR_RESIZE_NW;
	case LAB_EDGE_TOP:
		return LAB_CURSOR_RESIZE_N;
	case LAB_EDGES_TOP_RIGHT:
		return LAB_CURSOR_RESIZE_NE;
	case LAB_EDGE_RIGHT:
		return LAB_CURSOR_RESIZE_E;
	case LAB_EDGES_BOTTOM_RIGHT:
		return LAB_CURSOR_RESIZE_SE;
	case LAB_EDGE_BOTTOM:
		return LAB_CURSOR_RESIZE_S;
	case LAB_EDGES_BOTTOM_LEFT:
		return LAB_CURSOR_RESIZE_SW;
	case LAB_EDGE_LEFT:
		return LAB_CURSOR_RESIZE_W;
	default:
		return LAB_CURSOR_DEFAULT;
	}
}

static enum lab_cursors
cursor_get_from_ssd(enum lab_node_type view_area)
{
	enum lab_edge resize_edges = node_type_to_edges(view_area);
	return cursor_get_from_edge(resize_edges);
}

static struct wlr_surface *
get_toplevel(struct wlr_surface *surface)
{
	while (surface) {
		struct wlr_xdg_surface *xdg_surface =
			wlr_xdg_surface_try_from_wlr_surface(surface);
		if (!xdg_surface) {
			break;
		}

		switch (xdg_surface->role) {
		case WLR_XDG_SURFACE_ROLE_NONE:
			return NULL;
		case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
			return surface;
		case WLR_XDG_SURFACE_ROLE_POPUP:
			surface = xdg_surface->popup->parent;
			continue;
		}
	}
	if (surface && wlr_layer_surface_v1_try_from_wlr_surface(surface)) {
		return surface;
	}
	return NULL;
}

static void
handle_request_set_cursor(struct wl_listener *listener, void *data)
{
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		/* Prevent setting a cursor image when moving or resizing */
		return;
	}

	/*
	 * Omit cursor notifications when the current cursor is
	 * invisible, e.g. on touch input.
	 */
	if (!g_seat.cursor_visible) {
		return;
	}

	/*
	 * Omit cursor notifications from a pointer when a tablet
	 * tool (stylus/pen) is in proximity. We expect to get cursor
	 * notifications from the tablet tool instead.
	 * Receiving cursor notifications from pointer and tablet tool at
	 * the same time is a side effect of also setting pointer focus
	 * when a tablet tool enters proximity on a tablet-capable surface.
	 * See also `notify_motion()` in `input/tablet.c`.
	 */
	if (tablet_tool_has_focused_surface()) {
		return;
	}

	/*
	 * This event is raised by the seat when a client provides a cursor
	 * image
	 */
	auto event = (wlr_seat_pointer_request_set_cursor_event *)data;
	struct wlr_seat_client *focused_client =
		g_seat.seat->pointer_state.focused_client;

	/*
	 * This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first.
	 */
	if (focused_client == event->seat_client) {
		/*
		 * Once we've vetted the client, we can tell the cursor to use
		 * the provided surface as the cursor image. It will set the
		 * hardware cursor on the output that it's currently on and
		 * continue to do so as the cursor moves between outputs.
		 */

		wlr_cursor_set_surface(g_seat.cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void
handle_request_set_shape(struct wl_listener *listener, void *data)
{
	auto event = (wlr_cursor_shape_manager_v1_request_set_shape_event *)data;
	const char *shape_name = wlr_cursor_shape_v1_name(event->shape);
	struct wlr_seat_client *focused_client =
		g_seat.seat->pointer_state.focused_client;

	/* Prevent setting a cursor image when moving or resizing */
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	/*
	 * Omit set shape when the current cursor is
	 * invisible, e.g. on touch input.
	 */
	if (!g_seat.cursor_visible) {
		return;
	}

	/*
	 * This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first.
	 */
	if (event->seat_client != focused_client) {
		wlr_log(WLR_INFO, "seat client %p != focused client %p",
			event->seat_client, focused_client);
		return;
	}

	/*
	 * Omit cursor notifications from a pointer when a tablet
	 * tool (stylus/pen) is in proximity.
	 */
	if (tablet_tool_has_focused_surface() && event->device_type
			!= WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_TABLET_TOOL) {
		return;
	}

	wlr_log(WLR_DEBUG, "set xcursor to shape %s", shape_name);
	wlr_cursor_set_xcursor(g_seat.cursor, g_seat.xcursor_manager,
		shape_name);
}

static void
handle_request_set_selection(struct wl_listener *listener, void *data)
{
	auto event = (wlr_seat_request_set_selection_event *)data;
	wlr_seat_set_selection(g_seat.seat, event->source, event->serial);
}

static void
handle_request_set_primary_selection(struct wl_listener *listener, void *data)
{
	auto event = (wlr_seat_request_set_primary_selection_event *)data;
	wlr_seat_set_primary_selection(g_seat.seat, event->source,
		event->serial);
}

static void
process_cursor_move(uint32_t time)
{
	ASSERT_PTR(g_server.grabbed_view, view);

	int x = g_server.grab_box.x + (g_seat.cursor->x - g_server.grab_x);
	int y = g_server.grab_box.y + (g_seat.cursor->y - g_server.grab_y);

	/* Apply resistance for maximized/tiled view */
	bool needs_untile = resistance_unsnap_apply(view, &x, &y);
	if (needs_untile) {
		/*
		 * When the view needs to be un-tiled, resize it to natural
		 * geometry while anchoring it to cursor. If the natural
		 * geometry is unknown (possible with xdg-shell views), then
		 * we set a size of 0x0 here and determine the correct geometry
		 * later. See do_late_positioning() in xdg.c.
		 */
		struct wlr_box new_geo = {
			.width = view->natural_geometry.width,
			.height = view->natural_geometry.height,
		};
		interactive_anchor_to_cursor(&new_geo);
		/* Shaded clients will not process resize events until unshaded */
		view_set_shade(view, false);
		view_set_maximized(view, VIEW_AXIS_NONE);
		view_set_untiled(view);
		view_move_resize(view, new_geo);
		x = new_geo.x;
		y = new_geo.y;
	}

	/* Then apply window & edge resistance */
	resistance_move_apply(view, &x, &y);

	view_move(view, x, y);
	overlay_update(view);
}

static void
process_cursor_resize(uint32_t time)
{
	/* Rate-limit resize events respecting monitor refresh rate */
	static uint32_t last_resize_time = 0;
	static struct view *last_resize_view = NULL;

	ASSERT_PTR(g_server.grabbed_view, view);

	if (view == last_resize_view) {
		int32_t refresh = 0;
		if (output_is_usable(last_resize_view->output)) {
			refresh = last_resize_view->output->wlr_output->refresh;
		}
		/* Limit to 250Hz if refresh rate is not available */
		if (refresh <= 0) {
			refresh = 250000;
		}
		/* Not caring overflow, but it won't be observable */
		if (time - last_resize_time < 1000000 / (uint32_t)refresh) {
			return;
		}
	}

	last_resize_time = time;
	last_resize_view = view;

	double dx = g_seat.cursor->x - g_server.grab_x;
	double dy = g_seat.cursor->y - g_server.grab_y;

	struct wlr_box new_view_geo = view->current;

	if (g_server.resize_edges & LAB_EDGE_TOP) {
		/* Shift y to anchor bottom edge when resizing top */
		new_view_geo.y = g_server.grab_box.y + dy;
		new_view_geo.height = g_server.grab_box.height - dy;
	} else if (g_server.resize_edges & LAB_EDGE_BOTTOM) {
		new_view_geo.height = g_server.grab_box.height + dy;
	}

	if (g_server.resize_edges & LAB_EDGE_LEFT) {
		/* Shift x to anchor right edge when resizing left */
		new_view_geo.x = g_server.grab_box.x + dx;
		new_view_geo.width = g_server.grab_box.width - dx;
	} else if (g_server.resize_edges & LAB_EDGE_RIGHT) {
		new_view_geo.width = g_server.grab_box.width + dx;
	}

	resistance_resize_apply(view, &new_view_geo);
	view_adjust_size(view, &new_view_geo.width, &new_view_geo.height);

	if (g_server.resize_edges & LAB_EDGE_TOP) {
		/* After size adjustments, make sure to anchor bottom edge */
		new_view_geo.y = g_server.grab_box.y + g_server.grab_box.height
			- new_view_geo.height;
	}

	if (g_server.resize_edges & LAB_EDGE_LEFT) {
		/* After size adjustments, make sure to anchor bottom right */
		new_view_geo.x = g_server.grab_box.x + g_server.grab_box.width
			- new_view_geo.width;
	}

	if (rc.resize_draw_contents) {
		view_move_resize(view, new_view_geo);
	} else {
		resize_outlines_update(view, new_view_geo);
	}
}

void
cursor_set(enum lab_cursors cursor)
{
	assert(cursor > LAB_CURSOR_CLIENT && cursor < LAB_CURSOR_COUNT);

	/* Prevent setting the same cursor image twice */
	if (g_seat.server_cursor == cursor) {
		return;
	}

	if (g_seat.cursor_visible) {
		wlr_cursor_set_xcursor(g_seat.cursor, g_seat.xcursor_manager,
			cursor_names[cursor]);
	}
	g_seat.server_cursor = cursor;
}

void
cursor_set_visible(bool visible)
{
	if (g_seat.cursor_visible == visible) {
		return;
	}

	g_seat.cursor_visible = visible;
	cursor_update_image();
}

void
cursor_update_image(void)
{
	enum lab_cursors cursor = g_seat.server_cursor;

	if (!g_seat.cursor_visible) {
		wlr_cursor_unset_image(g_seat.cursor);
		return;
	}

	if (cursor == LAB_CURSOR_CLIENT) {
		/*
		 * When we loose the output cursor while over a client
		 * surface (e.g. output was destroyed and we now deal with
		 * a new output instance), we have to force a re-enter of
		 * the surface so the client sets its own cursor again.
		 */
		if (g_seat.seat->pointer_state.focused_surface) {
			g_seat.server_cursor = LAB_CURSOR_DEFAULT;
			wlr_cursor_set_xcursor(g_seat.cursor,
				g_seat.xcursor_manager, "");
			wlr_seat_pointer_clear_focus(g_seat.seat);
			cursor_update_focus();
		}
		return;
	}
	/*
	 * Call wlr_cursor_unset_image() first to force wlroots to
	 * update the cursor (e.g. for a new output). Otherwise,
	 * wlr_cursor_set_xcursor() may detect that we are setting the
	 * same cursor as before, and do nothing.
	 */
	wlr_cursor_unset_image(g_seat.cursor);
	wlr_cursor_set_xcursor(g_seat.cursor, g_seat.xcursor_manager,
		cursor_names[cursor]);
}

static bool
update_pressed_surface(struct cursor_context *ctx)
{
	/*
	 * In most cases, we don't want to leave one surface and enter
	 * another while a button is pressed.  We only do so when
	 * (1) there is a pointer grab active (e.g. XDG popup grab) and
	 * (2) both surfaces belong to the same XDG toplevel.
	 *
	 * GTK/Wayland menus are known to use an XDG popup grab and to
	 * rely on the leave/enter events to work properly.  Firefox
	 * context menus (in contrast) do not use an XDG popup grab and
	 * do not work properly if we send leave/enter events.
	 */
	if (!wlr_seat_pointer_has_grab(g_seat.seat)) {
		return false;
	}
	if (g_seat.pressed.surface && ctx->surface != g_seat.pressed.surface) {
		struct wlr_surface *toplevel = get_toplevel(ctx->surface);
		if (toplevel && toplevel == get_toplevel(g_seat.pressed.surface)) {
			seat_set_pressed(ctx);
			return true;
		}
	}
	return false;
}

static bool
process_cursor_motion_out_of_surface(double *sx, double *sy)
{
	struct view *view = g_seat.pressed.view;
	struct wlr_scene_node *node = g_seat.pressed.node;
	struct wlr_surface *surface = g_seat.pressed.surface;
	assert(surface);
	int lx, ly;

	if (node && wlr_subsurface_try_from_wlr_surface(surface)) {
		wlr_scene_node_coords(node, &lx, &ly);
	} else if (view) {
		lx = view->current.x;
		ly = view->current.y;
		/* Take into account invisible xdg-shell CSD borders */
		if (view->type == LAB_XDG_SHELL_VIEW) {
			struct wlr_xdg_surface *xdg_surface = xdg_surface_from_view(view);
			lx -= xdg_surface->geometry.x;
			ly -= xdg_surface->geometry.y;
		}
	} else if (node && wlr_layer_surface_v1_try_from_wlr_surface(surface)) {
		wlr_scene_node_coords(node, &lx, &ly);
#if HAVE_XWAYLAND
	} else if (node && node->parent == g_server.unmanaged_tree) {
		wlr_scene_node_coords(node, &lx, &ly);
#endif
	} else {
		wlr_log(WLR_ERROR, "Can't detect surface for out-of-surface movement");
		return false;
	}

	*sx = g_seat.cursor->x - lx;
	*sy = g_seat.cursor->y - ly;

	return true;
}

/*
 * Common logic shared by cursor_update_focus(), process_cursor_motion()
 * and cursor_axis()
 */
static bool
cursor_update_common(struct cursor_context *ctx, bool cursor_has_moved,
		double *sx, double *sy)
{
	struct wlr_seat *wlr_seat = g_seat.seat;

	ssd_update_hovered_button(ctx->node);

	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		/*
		 * Prevent updating focus/cursor image during
		 * interactive move/resize, window switcher and
		 * menu interaction.
		 */
		return false;
	}

	/* TODO: verify drag_icon logic */
	if (g_seat.pressed.surface && ctx->surface != g_seat.pressed.surface
			&& !update_pressed_surface(ctx)
			&& !g_seat.drag.active) {
		if (cursor_has_moved) {
			/*
			 * Button has been pressed while over another
			 * surface and is still held down.  Just send
			 * the motion events to the focused surface so
			 * we can keep scrolling or selecting text even
			 * if the cursor moves outside of the surface.
			 */
			return process_cursor_motion_out_of_surface(sx, sy);
		}
		return false;
	}

	if (ctx->surface) {
		/*
		 * Cursor is over an input-enabled client surface.  The
		 * cursor image will be set by request_cursor_notify()
		 * in response to the enter event.
		 */
		wlr_seat_pointer_notify_enter(wlr_seat, ctx->surface,
			ctx->sx, ctx->sy);
		g_seat.server_cursor = LAB_CURSOR_CLIENT;
		if (cursor_has_moved) {
			*sx = ctx->sx;
			*sy = ctx->sy;
			return true;
		}
	} else {
		/*
		 * Cursor is over a server (labwc) surface.  Clear focus
		 * from the focused client (if any, no-op otherwise) and
		 * set the cursor image ourselves when not currently in
		 * a drag operation.
		 */
		wlr_seat_pointer_notify_clear_focus(wlr_seat);
		if (!g_seat.drag.active) {
			enum lab_cursors cursor = cursor_get_from_ssd(ctx->type);
			if (ctx->view && ctx->view->shaded && cursor > LAB_CURSOR_GRAB) {
				/* Prevent resize cursor on borders for shaded SSD */
				cursor = LAB_CURSOR_DEFAULT;
			}
			cursor_set(cursor);
		}
	}
	return false;
}

enum lab_edge
cursor_get_resize_edges(struct wlr_cursor *cursor, struct cursor_context *ctx)
{
	enum lab_edge resize_edges = node_type_to_edges(ctx->type);
	if (ctx->view && !resize_edges) {
		struct wlr_box box = ctx->view->current;
		resize_edges = lab_edge(resize_edges
			| ((int)cursor->x < box.x + box.width / 2
				? LAB_EDGE_LEFT : LAB_EDGE_RIGHT));
		resize_edges = lab_edge(resize_edges
			| ((int)cursor->y < box.y + box.height / 2
				? LAB_EDGE_TOP : LAB_EDGE_BOTTOM));
	}
	return resize_edges;
}

bool
cursor_process_motion(uint32_t time, double *sx, double *sy)
{
	/* If the mode is non-passthrough, delegate to those functions. */
	if (g_server.input_mode == LAB_INPUT_STATE_MOVE) {
		process_cursor_move(time);
		return false;
	} else if (g_server.input_mode == LAB_INPUT_STATE_RESIZE) {
		process_cursor_resize(time);
		return false;
	}

	/* Otherwise, find view under the pointer and send the event along */
	struct cursor_context ctx = get_cursor_context();

	if (ctx.type == LAB_NODE_MENUITEM) {
		menu_process_cursor_motion(ctx.node);
		cursor_set(LAB_CURSOR_DEFAULT);
		return false;
	}

	if (g_seat.drag.active) {
		dnd_icons_move(g_seat.cursor->x, g_seat.cursor->y);
	}

	for (auto &mousebind : rc.mousebinds) {
		if (ctx.type == LAB_NODE_CLIENT
				&& view_inhibits_actions(ctx.view,
					mousebind.actions)) {
			continue;
		}
		if (mousebind.mouse_event == MOUSE_ACTION_DRAG
				&& mousebind.pressed_in_context) {
			/*
			 * Use view and resize edges from the press
			 * event (not the motion event) to prevent
			 * moving/resizing the wrong view
			 */
			mousebind.pressed_in_context = false;
			actions_run(g_seat.pressed.view, mousebind.actions,
				&g_seat.pressed);
		}
	}

	struct wlr_surface *old_focused_surface =
		g_seat.seat->pointer_state.focused_surface;

	bool notify = cursor_update_common(&ctx,
		/* cursor_has_moved */ true, sx, sy);

	struct wlr_surface *new_focused_surface =
		g_seat.seat->pointer_state.focused_surface;

	if (rc.focus_follow_mouse && new_focused_surface
			&& old_focused_surface != new_focused_surface) {
		/*
		 * If followMouse=yes, update the keyboard focus when the
		 * cursor enters a surface
		 */
		desktop_focus_view_or_surface(
			view_from_wlr_surface(new_focused_surface),
			new_focused_surface, rc.raise_on_focus);
	}

	return notify;
}

static void
_cursor_update_focus(void)
{
	/* Focus surface under cursor if it isn't already focused */
	struct cursor_context ctx = get_cursor_context();

	if ((ctx.view || ctx.surface) && rc.focus_follow_mouse
			&& !rc.focus_follow_mouse_requires_movement) {
		/*
		 * Always focus the surface below the cursor when
		 * followMouse=yes and followMouseRequiresMovement=no.
		 */
		desktop_focus_view_or_surface(ctx.view, ctx.surface,
			rc.raise_on_focus);
	}

	double sx, sy;
	cursor_update_common(&ctx, /*cursor_has_moved*/ false, &sx, &sy);
}

void
cursor_update_focus(void)
{
	/* Prevent recursion via view_move_to_front() */
	static bool updating_focus = false;
	if (!updating_focus) {
		updating_focus = true;
		_cursor_update_focus();
		updating_focus = false;
	}
}

static void
warp_cursor_to_constraint_hint(struct wlr_pointer_constraint_v1 *constraint)
{
	CHECK_PTR_OR_RET(g_server.active_view, view);

	if (constraint->current.committed
			& WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
		double sx = constraint->current.cursor_hint.x;
		double sy = constraint->current.cursor_hint.y;
		wlr_cursor_warp(g_seat.cursor, NULL, view->current.x + sx,
			view->current.y + sy);

		/* Make sure we are not sending unnecessary surface movements */
		wlr_seat_pointer_warp(g_seat.seat, sx, sy);
	}
}

static void
handle_constraint_commit(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_constraint_v1 *constraint =
		g_seat.current_constraint;
	/* Prevents unused variable warning when compiled without asserts */
	(void)constraint;
	assert(constraint->surface == data);
}

constraint::~constraint()
{
	if (g_seat.current_constraint == wlr_constraint) {
		warp_cursor_to_constraint_hint(wlr_constraint);

		if (g_seat.constraint_commit.link.next) {
			wl_list_remove(&g_seat.constraint_commit.link);
		}
		wl_list_init(&g_seat.constraint_commit.link);
		g_seat.current_constraint = NULL;
	}
}

void
create_constraint(struct wl_listener *listener, void *data)
{
	auto wlr_constraint = (wlr_pointer_constraint_v1 *)data;
	auto constraint = new struct constraint();

	constraint->wlr_constraint = wlr_constraint;
	CONNECT_LISTENER(wlr_constraint, constraint, destroy);

	if (CHECK_PTR(g_server.active_view, view)
			&& view->surface == wlr_constraint->surface) {
		constrain_cursor(wlr_constraint);
	}
}

void
constrain_cursor(struct wlr_pointer_constraint_v1 *constraint)
{
	if (g_seat.current_constraint == constraint) {
		return;
	}
	wl_list_remove(&g_seat.constraint_commit.link);
	if (g_seat.current_constraint) {
		if (!constraint) {
			warp_cursor_to_constraint_hint(
				g_seat.current_constraint);
		}

		wlr_pointer_constraint_v1_send_deactivated(
			g_seat.current_constraint);
	}

	g_seat.current_constraint = constraint;

	if (!constraint) {
		wl_list_init(&g_seat.constraint_commit.link);
		return;
	}

	wlr_pointer_constraint_v1_send_activated(constraint);
	g_seat.constraint_commit.notify = handle_constraint_commit;
	wl_signal_add(&constraint->surface->events.commit,
		&g_seat.constraint_commit);
}

static void
apply_constraint(struct wlr_pointer *pointer, double *x, double *y)
{
	CHECK_PTR_OR_RET(g_server.active_view, view);

	if (!g_seat.current_constraint
			|| pointer->base.type != WLR_INPUT_DEVICE_POINTER) {
		return;
	}
	assert(g_seat.current_constraint->type
		== WLR_POINTER_CONSTRAINT_V1_CONFINED);

	double sx = g_seat.cursor->x;
	double sy = g_seat.cursor->y;

	sx -= view->current.x;
	sy -= view->current.y;

	double sx_confined, sy_confined;
	if (!wlr_region_confine(&g_seat.current_constraint->region, sx, sy,
			sx + *x, sy + *y, &sx_confined, &sy_confined)) {
		return;
	}

	*x = sx_confined - sx;
	*y = sy_confined - sy;
}

static bool
cursor_locked(struct wlr_pointer *pointer)
{
	return g_seat.current_constraint
		&& pointer->base.type == WLR_INPUT_DEVICE_POINTER
		&& g_seat.current_constraint->type
			== WLR_POINTER_CONSTRAINT_V1_LOCKED;
}

static void
preprocess_cursor_motion(struct wlr_pointer *pointer, uint32_t time_msec,
		double dx, double dy)
{
	if (cursor_locked(pointer)) {
		return;
	}
	apply_constraint(pointer, &dx, &dy);

	/*
	 * The cursor doesn't move unless we tell it to. The cursor
	 * automatically handles constraining the motion to the output
	 * layout, as well as any special configuration applied for the
	 * specific input device which generated the event. You can pass
	 * NULL for the device if you want to move the cursor around
	 * without any input.
	 */
	wlr_cursor_move(g_seat.cursor, &pointer->base, dx, dy);
	double sx, sy;
	bool notify = cursor_process_motion(time_msec, &sx, &sy);
	if (notify) {
		wlr_seat_pointer_notify_motion(g_seat.seat, time_msec, sx, sy);
	}
}

static double get_natural_scroll_factor(struct wlr_input_device *wlr_input_device)
{
	if (wlr_input_device_is_libinput(wlr_input_device)) {
		struct libinput_device *libinput_device =
			wlr_libinput_get_device_handle(wlr_input_device);
		if (libinput_device_config_scroll_get_natural_scroll_enabled(libinput_device)) {
			return -1.0;
		}
	}

	return 1.0;
}

static void
handle_motion(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits a
	 * _relative_ pointer motion event (i.e. a delta)
	 */
	auto event = (wlr_pointer_motion_event *)data;
	idle_manager_notify_activity(g_seat.seat);
	cursor_set_visible(/* visible */ true);

	if (g_seat.cursor_scroll_wheel_emulation) {
		enum wl_pointer_axis orientation;
		double delta;
		if (fabs(event->delta_x) > fabs(event->delta_y)) {
			orientation = WL_POINTER_AXIS_HORIZONTAL_SCROLL;
			delta = event->delta_x;
		} else {
			orientation = WL_POINTER_AXIS_VERTICAL_SCROLL;
			delta = event->delta_y;
		}

		/*
		 * arbitrary factor that should give reasonable speed
		 * with the default configured scroll factor of 1.0
		 */
		double motion_to_scroll_factor = 0.04;
		double scroll_factor = motion_to_scroll_factor *
			get_natural_scroll_factor(&event->pointer->base);

		/* The delta of a single step for mouse wheel emulation */
		double pointer_axis_step = 15.0;

		cursor_emulate_axis(&event->pointer->base, orientation,
			pointer_axis_step * scroll_factor * delta, 0,
			WL_POINTER_AXIS_SOURCE_CONTINUOUS, event->time_msec);
	} else {
		wlr_relative_pointer_manager_v1_send_relative_motion(
			g_server.relative_pointer_manager, g_seat.seat,
			(uint64_t)event->time_msec * 1000, event->delta_x,
			event->delta_y, event->unaccel_dx, event->unaccel_dy);

		preprocess_cursor_motion(event->pointer, event->time_msec,
			event->delta_x, event->delta_y);
	}
}

static void
handle_motion_absolute(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an
	 * _absolute_ motion event, from 0..1 on each axis. This happens, for
	 * example, when wlroots is running under a Wayland window rather than
	 * KMS+DRM, and you move the mouse over the window. You could enter the
	 * window from any edge, so we have to warp the mouse there. There is
	 * also some hardware which emits these events.
	 */
	auto event = (wlr_pointer_motion_absolute_event *)data;
	idle_manager_notify_activity(g_seat.seat);
	cursor_set_visible(/* visible */ true);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(g_seat.cursor,
		&event->pointer->base, event->x, event->y, &lx, &ly);

	double dx = lx - g_seat.cursor->x;
	double dy = ly - g_seat.cursor->y;

	wlr_relative_pointer_manager_v1_send_relative_motion(
		g_server.relative_pointer_manager, g_seat.seat,
		(uint64_t)event->time_msec * 1000, dx, dy, dx, dy);

	preprocess_cursor_motion(event->pointer, event->time_msec, dx, dy);
}

static void
process_release_mousebinding(struct cursor_context *ctx, uint32_t button)
{
	if (g_server.input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER) {
		return;
	}

	uint32_t modifiers = keyboard_get_all_modifiers();

	for (auto &mousebind : rc.mousebinds) {
		if (ctx->type == LAB_NODE_CLIENT
				&& view_inhibits_actions(ctx->view,
					mousebind.actions)) {
			continue;
		}
		if (node_type_contains(mousebind.context, ctx->type)
				&& mousebind.button == button
				&& modifiers == mousebind.modifiers) {
			switch (mousebind.mouse_event) {
			case MOUSE_ACTION_RELEASE:
				break;
			case MOUSE_ACTION_CLICK:
				if (mousebind.pressed_in_context) {
					break;
				}
				continue;
			default:
				continue;
			}
			actions_run(ctx->view, mousebind.actions, ctx);
		}
	}
}

static bool
is_double_click(long double_click_speed, uint32_t button,
		struct cursor_context *ctx)
{
	static enum lab_node_type last_type;
	static uint32_t last_button;
	static struct view *last_view;
	static struct timespec last_click;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	long ms = (now.tv_sec - last_click.tv_sec) * 1000 +
		(now.tv_nsec - last_click.tv_nsec) / 1000000;
	last_click = now;
	if (last_button != button || last_view != ctx->view
			|| last_type != ctx->type) {
		last_button = button;
		last_view = ctx->view;
		last_type = ctx->type;
		return false;
	}
	if (ms < double_click_speed && ms >= 0) {
		/*
		 * End sequence so that third click is not considered a
		 * double-click
		 */
		last_button = 0;
		last_view = NULL;
		last_type = LAB_NODE_NONE;
		return true;
	}
	return false;
}

static bool
process_press_mousebinding(struct cursor_context *ctx, uint32_t button)
{
	if (g_server.input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER) {
		return false;
	}

	bool double_click = is_double_click(rc.doubleclick_time, button, ctx);
	bool consumed_by_frame_context = false;
	uint32_t modifiers = keyboard_get_all_modifiers();

	for (auto &mousebind : rc.mousebinds) {
		if (ctx->type == LAB_NODE_CLIENT
				&& view_inhibits_actions(ctx->view,
					mousebind.actions)) {
			continue;
		}
		if (node_type_contains(mousebind.context, ctx->type)
				&& mousebind.button == button
				&& modifiers == mousebind.modifiers) {
			switch (mousebind.mouse_event) {
			case MOUSE_ACTION_DRAG: /* fallthrough */
			case MOUSE_ACTION_CLICK:
				/*
				 * DRAG and CLICK actions will be processed on
				 * the release event, unless the press event is
				 * counted as a DOUBLECLICK.
				 */
				if (!double_click) {
					/* Swallow the press event */
					consumed_by_frame_context |=
						mousebind.context == LAB_NODE_FRAME;
					consumed_by_frame_context |=
						mousebind.context == LAB_NODE_ALL;
					mousebind.pressed_in_context = true;
				}
				continue;
			case MOUSE_ACTION_DOUBLECLICK:
				if (!double_click) {
					continue;
				}
				break;
			case MOUSE_ACTION_PRESS:
				break;
			default:
				continue;
			}
			consumed_by_frame_context |=
				mousebind.context == LAB_NODE_FRAME;
			consumed_by_frame_context |=
				mousebind.context == LAB_NODE_ALL;
			actions_run(ctx->view, mousebind.actions, ctx);
		}
	}
	return consumed_by_frame_context;
}

static struct wlr_layer_surface_v1 *
get_root_layer(struct wlr_surface *wlr_surface)
{
	assert(wlr_surface);
	struct wlr_subsurface *subsurface =
		wlr_subsurface_try_from_wlr_surface(wlr_surface);
	if (subsurface) {
		if (subsurface->parent) {
			return get_root_layer(subsurface->parent);
		} else {
			/* never reached? */
			wlr_log(WLR_ERROR, "subsurface without parent");
			return NULL;
		}
	} else {
		return wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface);
	}
}

static uint32_t press_msec;

bool
cursor_process_button_press(uint32_t button, uint32_t time_msec)
{
	struct cursor_context ctx = get_cursor_context();

	/* Used on next button release to check if it can close menu or select menu item */
	press_msec = time_msec;

	if (ctx.view || ctx.surface) {
		/* Store cursor context for later action processing */
		seat_set_pressed(&ctx);
	}

	if (g_server.input_mode == LAB_INPUT_STATE_MENU) {
		/*
		 * If menu was already opened on press, set a very small value
		 * so subsequent release always closes menu or selects menu item.
		 */
		press_msec = 0;
		lab_set_add(&g_seat.bound_buttons, button);
		return false;
	}

	/*
	 * On press, set focus to a non-view surface that wants it.
	 * Action processing does not run for these surfaces and thus
	 * the Focus action (used for normal views) does not work.
	 */
	if (ctx.type == LAB_NODE_LAYER_SURFACE) {
		wlr_log(WLR_DEBUG, "press on layer-(sub)surface");
		struct wlr_layer_surface_v1 *layer = get_root_layer(ctx.surface);
		if (layer && layer->current.keyboard_interactive) {
			layer_try_set_focus(layer);
		}
#ifdef HAVE_XWAYLAND
	} else if (ctx.type == LAB_NODE_UNMANAGED) {
		desktop_focus_view_or_surface(NULL, ctx.surface,
			/*raise*/ false);
#endif
	}

	if (ctx.type != LAB_NODE_CLIENT && ctx.type != LAB_NODE_LAYER_SURFACE
			&& wlr_seat_pointer_has_grab(g_seat.seat)) {
		/*
		 * If we have an active popup grab (an open popup) we want to
		 * cancel that grab whenever the user presses on anything that
		 * is not the client itself, for example the desktop or any
		 * part of the server side decoration.
		 *
		 * Note: This does not work for XWayland clients
		 */
		wlr_seat_pointer_end_grab(g_seat.seat);
		lab_set_add(&g_seat.bound_buttons, button);
		return false;
	}

	/* Bindings to the Frame context swallow mouse events if activated */
	bool consumed_by_frame_context =
		process_press_mousebinding(&ctx, button);

	if (ctx.surface && !consumed_by_frame_context) {
		/* Notify client with pointer focus of button press */
		return true;
	}

	lab_set_add(&g_seat.bound_buttons, button);
	return false;
}

bool
cursor_process_button_release(uint32_t button, uint32_t time_msec)
{
	struct cursor_context ctx = get_cursor_context();
	struct wlr_surface *pressed_surface = g_seat.pressed.surface;

	/* Always notify button release event when it's not bound */
	const bool notify = !lab_set_contains(&g_seat.bound_buttons, button);

	seat_reset_pressed();

	if (g_server.input_mode == LAB_INPUT_STATE_MENU) {
		/* TODO: take into account overflow of time_msec */
		if (time_msec - press_msec > rc.menu_ignore_button_release_period) {
			if (ctx.type == LAB_NODE_MENUITEM) {
				menu_call_selected_actions();
			} else {
				menu_close_root();
				cursor_update_focus();
			}
		}
		return notify;
	}

	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return notify;
	}

	if (pressed_surface && ctx.surface != pressed_surface) {
		/*
		 * Button released but originally pressed over a different surface.
		 * Just send the release event to the still focused surface.
		 */
		return notify;
	}

	process_release_mousebinding(&ctx, button);

	return notify;
}

bool
cursor_finish_button_release(uint32_t button)
{
	/* Clear "pressed" status for all bindings of this mouse button */
	for (auto &mousebind : rc.mousebinds) {
		if (mousebind.button == button) {
			mousebind.pressed_in_context = false;
		}
	}

	lab_set_remove(&g_seat.bound_buttons, button);

	if (g_server.input_mode == LAB_INPUT_STATE_MOVE
			|| g_server.input_mode == LAB_INPUT_STATE_RESIZE) {
		ASSERT_PTR(g_server.grabbed_view, view);
		if (resize_outlines_enabled(view)) {
			resize_outlines_finish(view);
		}
		/* Exit interactive move/resize mode */
		interactive_finish(view);
		return true;
	}

	return false;
}

static void
handle_button(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits a button
	 * event.
	 */
	auto event = (wlr_pointer_button_event *)data;
	idle_manager_notify_activity(g_seat.seat);
	cursor_set_visible(/* visible */ true);

	bool notify;
	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		notify = cursor_process_button_press(event->button,
			event->time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(g_seat.seat,
				event->time_msec, event->button, event->state);
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		notify = cursor_process_button_release(event->button,
			event->time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(g_seat.seat,
				event->time_msec, event->button, event->state);
		}
		cursor_finish_button_release(event->button);
		break;
	}
}

struct scroll_info {
	int direction;
	bool run_action;
};

static struct scroll_info
compare_delta(double delta, double delta_discrete, struct accumulated_scroll *accum)
{
	struct scroll_info info = {0};

	if (delta_discrete) {
		/* mice */
		info.direction = delta_discrete > 0 ? 1 : -1;
		accum->delta_discrete += delta_discrete;
		/*
		 * Non-hi-res mice produce delta_discrete of ±120 for every
		 * "click", so it always triggers actions. But for hi-res mice
		 * that produce smaller delta_discrete, we accumulate it and
		 * run actions after it exceeds 120(= 1 click).
		 */
		if (fabs(accum->delta_discrete) >= 120.0) {
			accum->delta_discrete = fmod(accum->delta_discrete, 120.0);
			info.run_action = true;
		}
	} else {
		/* 2-finger scrolling on touchpads */
		if (delta == 0) {
			/* delta=0 marks the end of a scroll */
			accum->delta = 0;
			return info;
		}
		info.direction = delta > 0 ? 1 : -1;
		accum->delta += delta;
		/*
		 * The threshold of 10 is inherited from various historic
		 * projects including weston.
		 *
		 * For historic context, see:
		 * https://lists.freedesktop.org/archives/wayland-devel/2019-April/040377.html
		 */
		if (fabs(accum->delta) >= 10.0) {
			accum->delta = fmod(accum->delta, 10.0);
			info.run_action = true;
		}
	}

	return info;
}

static bool
process_cursor_axis(enum wl_pointer_axis orientation, double delta,
		double delta_discrete)
{
	struct cursor_context ctx = get_cursor_context();
	uint32_t modifiers = keyboard_get_all_modifiers();

	enum direction direction = LAB_DIRECTION_INVALID;
	struct scroll_info info = compare_delta(delta, delta_discrete,
		&g_seat.accumulated_scrolls[orientation]);

	if (orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
		if (info.direction < 0) {
			direction = LAB_DIRECTION_LEFT;
		} else if (info.direction > 0) {
			direction = LAB_DIRECTION_RIGHT;
		}
	} else if (orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		if (info.direction < 0) {
			direction = LAB_DIRECTION_UP;
		} else if (info.direction > 0) {
			direction = LAB_DIRECTION_DOWN;
		}
	} else {
		wlr_log(WLR_DEBUG, "Failed to handle cursor axis event");
	}

	bool handled = false;
	if (direction != LAB_DIRECTION_INVALID) {
		for (auto &mousebind : rc.mousebinds) {
			if (ctx.type == LAB_NODE_CLIENT
					&& view_inhibits_actions(ctx.view,
						mousebind.actions)) {
				continue;
			}
			if (node_type_contains(mousebind.context, ctx.type)
					&& mousebind.direction == direction
					&& modifiers == mousebind.modifiers
					&& mousebind.mouse_event
						== MOUSE_ACTION_SCROLL) {
				handled = true;
				/*
				 * Action may not be executed if the accumulated scroll delta
				 * on touchpads or hi-res mice doesn't exceed the threshold
				 */
				if (info.run_action) {
					actions_run(ctx.view, mousebind.actions,
						&ctx);
				}
			}
		}
	}

	/* Bindings swallow mouse events if activated */
	if (ctx.surface && !handled) {
		/* Make sure we are sending the events to the surface under the cursor */
		double sx, sy;
		cursor_update_common(&ctx, /*cursor_has_moved*/ false, &sx,
			&sy);

		return true;
	}

	return false;
}

static void
handle_axis(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an axis
	 * event, for example when you move the scroll wheel.
	 */
	auto event = (wlr_pointer_axis_event *)data;
	idle_manager_notify_activity(g_seat.seat);
	cursor_set_visible(/* visible */ true);

	/* input->scroll_factor is set for pointer/touch devices */
	assert(event->pointer->base.type == WLR_INPUT_DEVICE_POINTER
		|| event->pointer->base.type == WLR_INPUT_DEVICE_TOUCH);
	auto input = (::input *)event->pointer->base.data;
	double scroll_factor = input->scroll_factor;

	bool notify = process_cursor_axis(event->orientation, event->delta,
		event->delta_discrete);

	if (notify) {
		/* Notify the client with pointer focus of the axis event. */
		wlr_seat_pointer_notify_axis(g_seat.seat, event->time_msec,
			event->orientation, scroll_factor * event->delta,
			round(scroll_factor * event->delta_discrete),
			event->source, event->relative_direction);
	}
}

static void
handle_frame(struct wl_listener *listener, void *data)
{
	/*
	 * This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen
	 * at the same time, in which case a frame event won't be sent in
	 * between.
	 */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(g_seat.seat);
}

void
cursor_emulate_axis(struct wlr_input_device *device,
		enum wl_pointer_axis orientation, double delta,
		double delta_discrete, enum wl_pointer_axis_source source,
		uint32_t time_msec)
{
	auto input = (::input *)device->data;

	double scroll_factor = 1.0;
	/* input->scroll_factor is set for pointer/touch devices */
	if (device->type == WLR_INPUT_DEVICE_POINTER
			|| device->type == WLR_INPUT_DEVICE_TOUCH) {
		scroll_factor = input->scroll_factor;
	}

	bool notify = process_cursor_axis(orientation, delta, delta_discrete);
	if (notify) {
		/* Notify the client with pointer focus of the axis event. */
		wlr_seat_pointer_notify_axis(g_seat.seat, time_msec,
			orientation, scroll_factor * delta,
			round(scroll_factor * delta_discrete), source,
			WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
	}
	wlr_seat_pointer_notify_frame(g_seat.seat);
}

void
cursor_emulate_move(struct wlr_input_device *device, double dx, double dy,
		uint32_t time_msec)
{
	if (!dx && !dy) {
		wlr_log(WLR_DEBUG, "dropping useless cursor_emulate: %.10f,%.10f", dx, dy);
		return;
	}

	wlr_relative_pointer_manager_v1_send_relative_motion(
		g_server.relative_pointer_manager, g_seat.seat,
		(uint64_t)time_msec * 1000, dx, dy, dx, dy);

	wlr_cursor_move(g_seat.cursor, device, dx, dy);
	double sx, sy;
	bool notify = cursor_process_motion(time_msec, &sx, &sy);
	if (notify) {
		wlr_seat_pointer_notify_motion(g_seat.seat, time_msec, sx, sy);
	}
	wlr_seat_pointer_notify_frame(g_seat.seat);
}

void
cursor_emulate_move_absolute(struct wlr_input_device *device, double x,
		double y, uint32_t time_msec)
{
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(g_seat.cursor, device, x, y, &lx,
		&ly);

	double dx = lx - g_seat.cursor->x;
	double dy = ly - g_seat.cursor->y;

	cursor_emulate_move(device, dx, dy, time_msec);
}

void
cursor_emulate_button(uint32_t button, enum wl_pointer_button_state state,
		uint32_t time_msec)
{
	bool notify;
	switch (state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		notify = cursor_process_button_press(button, time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(g_seat.seat, time_msec,
				button, state);
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		notify = cursor_process_button_release(button, time_msec);
		if (notify) {
			wlr_seat_pointer_notify_button(g_seat.seat, time_msec,
				button, state);
		}
		cursor_finish_button_release(button);
		break;
	}
	wlr_seat_pointer_notify_frame(g_seat.seat);
}

static void
cursor_load(void)
{
	const char *xcursor_theme = getenv("XCURSOR_THEME");
	const char *xcursor_size = getenv("XCURSOR_SIZE");
	uint32_t size = xcursor_size ? atoi(xcursor_size) : 24;

	if (g_seat.xcursor_manager) {
		wlr_xcursor_manager_destroy(g_seat.xcursor_manager);
	}
	g_seat.xcursor_manager =
		wlr_xcursor_manager_create(xcursor_theme, size);
	wlr_xcursor_manager_load(g_seat.xcursor_manager, 1);

	/*
	 * Wlroots provides integrated fallback cursor icons using
	 * old-style X11 cursor names (cursors_x11) and additionally
	 * (since wlroots 0.16.2) aliases them to cursor-spec names
	 * (cursors_xdg).
	 *
	 * However, the aliasing does not include the "grab" cursor
	 * icon which labwc uses when dragging a window. To fix that,
	 * try to get the grab cursor icon from wlroots. If the user
	 * supplied an appropriate cursor theme which includes the
	 * "grab" cursor icon, we will keep using it.
	 *
	 * If no "grab" icon can be found we will fall back to the
	 * old style cursor names and use "grabbing" instead which
	 * is part of the X11 fallbacks and thus always available.
	 *
	 * Shipping the complete alias table for X11 cursor names
	 * (and not just the "grab" cursor alias) makes sure that
	 * this also works for wlroots versions before 0.16.2.
	 *
	 * See the cursor name alias table on the top of this file
	 * for the actual cursor names used.
	 */
	if (wlr_xcursor_manager_get_xcursor(g_seat.xcursor_manager,
			cursors_xdg[LAB_CURSOR_GRAB], 1)) {
		cursor_names = cursors_xdg;
	} else {
		wlr_log(WLR_INFO,
			"Cursor theme is missing cursor names, using fallback");
		cursor_names = cursors_x11;
	}
}

void
cursor_reload(void)
{
	cursor_load();
#if HAVE_XWAYLAND
	xwayland_reset_cursor();
#endif
	cursor_update_image();
}

void
cursor_init(void)
{
	cursor_load();

	/* Set the initial cursor image so the cursor is visible right away */
	cursor_set(LAB_CURSOR_DEFAULT);

	dnd_init();

	CONNECT_SIGNAL(g_seat.cursor, &g_seat.on_cursor, motion);
	CONNECT_SIGNAL(g_seat.cursor, &g_seat.on_cursor, motion_absolute);
	CONNECT_SIGNAL(g_seat.cursor, &g_seat.on_cursor, button);
	CONNECT_SIGNAL(g_seat.cursor, &g_seat.on_cursor, axis);
	CONNECT_SIGNAL(g_seat.cursor, &g_seat.on_cursor, frame);

	gestures_init();
	touch_init();
	tablet_init();

	CONNECT_SIGNAL(g_seat.seat, &g_seat, request_set_cursor);

	struct wlr_cursor_shape_manager_v1 *cursor_shape_manager =
		wlr_cursor_shape_manager_v1_create(g_server.wl_display,
			LAB_CURSOR_SHAPE_V1_VERSION);
	if (!cursor_shape_manager) {
		wlr_log(WLR_ERROR, "unable to create cursor_shape interface");
		exit(EXIT_FAILURE);
	}

	CONNECT_SIGNAL(cursor_shape_manager, &g_seat, request_set_shape);
	CONNECT_SIGNAL(g_seat.seat, &g_seat, request_set_selection);
	CONNECT_SIGNAL(g_seat.seat, &g_seat, request_set_primary_selection);
}

void
cursor_finish(void)
{
	wl_list_remove(&g_seat.on_cursor.motion.link);
	wl_list_remove(&g_seat.on_cursor.motion_absolute.link);
	wl_list_remove(&g_seat.on_cursor.button.link);
	wl_list_remove(&g_seat.on_cursor.axis.link);
	wl_list_remove(&g_seat.on_cursor.frame.link);

	gestures_finish();
	touch_finish();

	tablet_finish();

	wl_list_remove(&g_seat.request_set_cursor.link);
	wl_list_remove(&g_seat.request_set_shape.link);
	wl_list_remove(&g_seat.request_set_selection.link);
	wl_list_remove(&g_seat.request_set_primary_selection.link);

	wlr_xcursor_manager_destroy(g_seat.xcursor_manager);
	wlr_cursor_destroy(g_seat.cursor);

	dnd_finish();
}
