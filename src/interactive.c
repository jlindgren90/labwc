// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_cursor.h>
#include "labwc.h"
#include "view.h"

/*
 * Called before interactive_begin() to set the initial grab parameters
 * (cursor position and view geometry). Once the cursor actually moves,
 * then interactive_begin() is called.
 */
void
interactive_set_grab_context(struct cursor_context *ctx)
{
	if (!ctx->view_id || g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	view_set_grab_context(ctx->view_id, g_seat.cursor->x, g_seat.cursor->y,
		cursor_get_resize_edges(g_seat.cursor, ctx));
}

void
interactive_begin(ViewId view_id, enum input_mode mode, enum lab_edge edges)
{
	/*
	 * This function sets up an interactive move or resize operation, where
	 * the compositor stops propagating pointer events to clients and
	 * instead consumes them itself, to move or resize windows.
	 */
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	enum lab_cursors cursor_shape = LAB_CURSOR_DEFAULT;

	switch (mode) {
	case LAB_INPUT_STATE_MOVE:
		if (!view_start_move(view_id)) {
			return;
		}
		cursor_shape = LAB_CURSOR_GRAB;
		break;
	case LAB_INPUT_STATE_RESIZE: {
		if (!view_start_resize(view_id, edges)) {
			return;
		}
		cursor_shape = cursor_get_from_edge(view_get_resize_edges());
		break;
	}
	default:
		/* Should not be reached */
		return;
	}

	seat_focus_override_begin(mode, cursor_shape);
}
