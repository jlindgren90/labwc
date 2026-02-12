// SPDX-License-Identifier: GPL-2.0-only
/* view-impl-common.c: common code for shell view->impl functions */
#include "view-impl-common.h"
#include "labwc.h"
#include "view.h"

void
view_notify_map(struct view *view)
{
	/* Leave minimized, if minimized before map */
	if (!view->st->minimized) {
		desktop_focus_view(view, /* raise */ true);
	}

	wlr_log(WLR_DEBUG, "[map] identifier=%s, title=%s", view->st->app_id,
		view->st->title);
}

void
view_notify_unmap(struct view *view)
{
	/*
	 * When exiting an xwayland application with multiple views
	 * mapped, a race condition can occur: after the topmost view
	 * is unmapped, the next view under it is offered focus, but is
	 * also unmapped before accepting focus (so server->active_view
	 * remains NULL). To avoid being left with no active view at
	 * all, check for that case also.
	 */
	struct view *active_view = view_get_active();
	if (view == active_view || !active_view) {
		desktop_focus_topmost_view();
	}
}

static bool
resizing_edge(struct view *view, enum lab_edge edge)
{
	return g_server.input_mode == LAB_INPUT_STATE_RESIZE
		&& g_server.grabbed_view == view
		&& (g_server.resize_edges & edge);
}

void
view_impl_apply_geometry(struct view *view, int w, int h)
{
	const struct wlr_box *current = &view->st->current;
	const struct wlr_box *pending = &view->st->pending;
	int x, y;

	/*
	 * Anchor right edge if resizing via left edge.
	 *
	 * Note that answering the question "are we resizing?" is a bit
	 * tricky. The most obvious method is to look at the server
	 * flags; but that method will not account for any late commits
	 * that occur after the mouse button is released, as the client
	 * catches up with pending configure requests. So as a fallback,
	 * we resort to a geometry-based heuristic -- also not 100%
	 * reliable on its own. The combination of the two methods
	 * should catch 99% of resize cases that we care about.
	 */
	bool resizing_left_edge = resizing_edge(view, LAB_EDGE_LEFT);
	if (resizing_left_edge || (current->x != pending->x
			&& current->x + current->width ==
			pending->x + pending->width)) {
		x = pending->x + pending->width - w;
	} else {
		x = pending->x;
	}

	/* Anchor bottom edge if resizing via top edge */
	bool resizing_top_edge = resizing_edge(view, LAB_EDGE_TOP);
	if (resizing_top_edge || (current->y != pending->y
			&& current->y + current->height ==
			pending->y + pending->height)) {
		y = pending->y + pending->height - h;
	} else {
		y = pending->y;
	}

	view_set_current_pos(view->id, x, y);
	view_set_current_size(view->id, w, h);
}
