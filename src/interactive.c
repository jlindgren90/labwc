// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_cursor.h>
#include "labwc.h"
#include "output.h"
#include "view.h"

#define SNAP_EDGE_RANGE 10

/*
 *   pos_old  pos_cursor
 *      v         v
 *      +---------+-------------------+
 *      <-----------size_old---------->
 *
 *      return value
 *           v
 *           +----+---------+
 *           <---size_new--->
 */
static int
max_move_scale(double pos_cursor, double pos_old, double size_old,
		double size_new)
{
	double anchor_frac = (pos_cursor - pos_old) / size_old;
	int pos_new = pos_cursor - (size_new * anchor_frac);
	if (pos_new < pos_old) {
		/* Clamp by using the old offsets of the maximized window */
		pos_new = pos_old;
	}
	return pos_new;
}

void
interactive_anchor_to_cursor(struct wlr_box *geo)
{
	assert(server.input_mode == LAB_INPUT_STATE_MOVE);
	if (wlr_box_empty(geo)) {
		return;
	}
	/* Resize grab_box while anchoring it to grab_{x,y} */
	server.grab_box.x = max_move_scale(server.grab_x, server.grab_box.x,
		server.grab_box.width, geo->width);
	server.grab_box.y = max_move_scale(server.grab_y, server.grab_box.y,
		server.grab_box.height, geo->height);
	server.grab_box.width = geo->width;
	server.grab_box.height = geo->height;

	geo->x = server.grab_box.x + (g_seat.cursor->x - server.grab_x);
	geo->y = server.grab_box.y + (g_seat.cursor->y - server.grab_y);
}

/*
 * Called before interactive_begin() to set the initial grab parameters
 * (cursor position and view geometry). Once the cursor actually moves,
 * then interactive_begin() is called.
 */
void
interactive_set_grab_context(struct cursor_context *ctx)
{
	if (!ctx->view) {
		return;
	}
	if (server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	server.grabbed_view = ctx->view;
	server.grab_x = g_seat.cursor->x;
	server.grab_y = g_seat.cursor->y;
	server.grab_box = ctx->view->st->current;
	server.resize_edges =
		cursor_get_resize_edges(g_seat.cursor, ctx);
}

void
interactive_begin(struct view *view, enum input_mode mode, enum lab_edge edges)
{
	assert(view);
	/*
	 * This function sets up an interactive move or resize operation, where
	 * the compositor stops propagating pointer events to clients and
	 * instead consumes them itself, to move or resize windows.
	 */

	if (server.input_mode != LAB_INPUT_STATE_PASSTHROUGH
			|| view != server.grabbed_view) {
		return;
	}

	/* Prevent moving/resizing panel-like views */
	if (view_has_strut_partial(view)) {
		return;
	}

	enum lab_cursors cursor_shape = LAB_CURSOR_DEFAULT;

	switch (mode) {
	case LAB_INPUT_STATE_MOVE:
		if (view->st->fullscreen) {
			/**
			 * We don't allow moving fullscreen windows.
			 *
			 * If you think there is a good reason to allow
			 * it, feel free to open an issue explaining
			 * your use-case.
			 */
			return;
		}

		/* Store natural geometry at start of move */
		view_store_natural_geom(view->id);

		cursor_shape = LAB_CURSOR_GRAB;
		break;
	case LAB_INPUT_STATE_RESIZE: {
		if (view->st->fullscreen || view->st->maximized == VIEW_AXIS_BOTH) {
			/*
			 * We don't allow resizing while fullscreen or
			 * maximized in both directions.
			 */
			return;
		}

		/*
		 * Override resize edges if specified explicitly.
		 * Otherwise, they were set already from cursor context.
		 */
		if (edges != LAB_EDGE_NONE) {
			server.resize_edges = edges;
		}

		/*
		 * If tiled or maximized in only one direction, reset
		 * tiled state and un-maximize the relevant axes, but
		 * keep the same geometry as the starting point.
		 */
		enum view_axis maximized = view->st->maximized;
		if (server.resize_edges & LAB_EDGES_LEFT_RIGHT) {
			maximized &= ~VIEW_AXIS_HORIZONTAL;
		}
		if (server.resize_edges & LAB_EDGES_TOP_BOTTOM) {
			maximized &= ~VIEW_AXIS_VERTICAL;
		}
		view_set_maximized(view->id, maximized);
		view_set_tiled(view->id, LAB_EDGE_NONE);
		cursor_shape = cursor_get_from_edge(server.resize_edges);
		break;
	}
	default:
		/* Should not be reached */
		return;
	}

	seat_focus_override_begin(mode, cursor_shape);
}

bool
edge_from_cursor(struct output **dest_output,
		enum lab_edge *edge1, enum lab_edge *edge2)
{
	*dest_output = NULL;
	*edge1 = LAB_EDGE_NONE;
	*edge2 = LAB_EDGE_NONE;

	if (!view_is_floating(server.grabbed_view->st)) {
		return false;
	}

	struct output *output = output_nearest_to_cursor();
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "output at cursor is unusable");
		return false;
	}
	*dest_output = output;

	double cursor_x = g_seat.cursor->x;
	double cursor_y = g_seat.cursor->y;

	/* Translate into output local coordinates */
	wlr_output_layout_output_coords(server.output_layout,
		output->wlr_output, &cursor_x, &cursor_y);

	struct wlr_box *area = &output->usable_area;

	int top = cursor_y - area->y;
	int bottom = area->y + area->height - cursor_y;
	int left = cursor_x - area->x;
	int right = area->x + area->width - cursor_x;

	if (top < SNAP_EDGE_RANGE) {
		*edge1 = LAB_EDGE_TOP;
	} else if (bottom < SNAP_EDGE_RANGE) {
		*edge1 = LAB_EDGE_BOTTOM;
	} else if (left < SNAP_EDGE_RANGE) {
		*edge1 = LAB_EDGE_LEFT;
	} else if (right < SNAP_EDGE_RANGE) {
		*edge1 = LAB_EDGE_RIGHT;
	} else {
		return false;
	}

	if (*edge1 == LAB_EDGE_TOP || *edge1 == LAB_EDGE_BOTTOM) {
		if (left < SNAP_EDGE_RANGE) {
			*edge2 = LAB_EDGE_LEFT;
		} else if (right < SNAP_EDGE_RANGE) {
			*edge2 = LAB_EDGE_RIGHT;
		}
	} else if (*edge1  == LAB_EDGE_LEFT || *edge1 == LAB_EDGE_RIGHT) {
		if (top < SNAP_EDGE_RANGE) {
			*edge2 = LAB_EDGE_TOP;
		} else if (bottom < SNAP_EDGE_RANGE) {
			*edge2 = LAB_EDGE_BOTTOM;
		}
	}

	return true;
}

/* Returns true if view was snapped to any edge */
static bool
snap_to_edge(struct view *view)
{
	struct output *output;
	enum lab_edge edge1, edge2;
	if (!edge_from_cursor(&output, &edge1, &edge2)) {
		return false;
	}
	enum lab_edge edge = edge1 | edge2;

	view_set_output(view->id, output);
	if (edge == LAB_EDGE_TOP) {
		view_maximize(view->id, VIEW_AXIS_BOTH);
	} else {
		view_tile(view->id, edge);
	}

	return true;
}

void
interactive_finish(struct view *view)
{
	assert(view);

	if (server.grabbed_view != view) {
		return;
	}

	if (server.input_mode == LAB_INPUT_STATE_MOVE) {
		snap_to_edge(view);
	}

	interactive_cancel(view);
}

/*
 * Cancels interactive move/resize without changing the state of the of
 * the view in any way. This may leave the tiled state inconsistent with
 * the actual geometry of the view.
 */
void
interactive_cancel(struct view *view)
{
	assert(view);

	if (server.grabbed_view != view) {
		return;
	}

	server.grabbed_view = NULL;

	/*
	 * It's possible that grabbed_view was set but interactive_begin()
	 * wasn't called yet. In that case, we are done.
	 */
	if (server.input_mode != LAB_INPUT_STATE_MOVE
			&& server.input_mode != LAB_INPUT_STATE_RESIZE) {
		return;
	}

	/* Restore keyboard/pointer focus */
	seat_focus_override_end(/*restore_focus*/ true);
}

bool
interactive_move_is_active(struct view *view)
{
	return (server.input_mode == LAB_INPUT_STATE_MOVE
		&& server.grabbed_view == view);
}
