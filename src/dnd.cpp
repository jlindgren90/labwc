// SPDX-License-Identifier: GPL-2.0-only
#include "dnd.h"
#include <assert.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "config/rcxml.h"
#include "input/cursor.h"
#include "labwc.h"  /* for struct seat */
#include "view.h"

/* Internal DnD handlers */
static void
handle_drag_request(struct wl_listener *listener, void *data)
{
	auto event = (wlr_seat_request_start_drag_event *)data;

	if (wlr_seat_validate_pointer_grab_serial(g_seat.seat, event->origin,
			event->serial)) {
		wlr_seat_start_pointer_drag(g_seat.seat, event->drag,
			event->serial);
	} else {
		wlr_data_source_destroy(event->drag->source);
		wlr_log(WLR_ERROR, "wrong source for drag request");
	}
}

static void
handle_drag_start(struct wl_listener *listener, void *data)
{
	assert(!g_seat.drag.active);
	auto drag = (wlr_drag *)data;

	g_seat.drag.active = true;
	cursor_context_save(&g_seat.pressed, NULL);
	if (drag->icon) {
		/* Cleans up automatically on drag->icon->events.destroy */
		wlr_scene_drag_icon_create(g_seat.drag.icons, drag->icon);
		wlr_scene_node_raise_to_top(&g_seat.drag.icons->node);
		wlr_scene_node_set_enabled(&g_seat.drag.icons->node, true);
	}
	wl_signal_add(&drag->events.destroy, &g_seat.drag.events.destroy);
}

static void
handle_drag_destroy(struct wl_listener *listener, void *data)
{
	assert(g_seat.drag.active);

	g_seat.drag.active = false;
	wl_list_remove(&g_seat.drag.events.destroy.link);
	wlr_scene_node_set_enabled(&g_seat.drag.icons->node, false);

	/*
	 * The default focus behaviour at the end of a dnd operation is that the
	 * window that originally had keyboard-focus retains that focus. This is
	 * consistent with the default behaviour of openbox and mutter.
	 *
	 * However, if the 'focus/followMouse' option is enabled we need to
	 * refocus the current surface under the cursor because keyboard focus
	 * is not changed during drag.
	 */
	if (!rc.focus_follow_mouse) {
		return;
	}

	struct cursor_context ctx = get_cursor_context();
	if (!ctx.surface) {
		return;
	}
	seat_focus_surface(NULL);
	seat_focus_surface(ctx.surface);

	if (ctx.view && rc.raise_on_focus) {
		view_move_to_front(ctx.view);
	}
}

/* Public API */
void
dnd_init(void)
{
	g_seat.drag.icons = wlr_scene_tree_create(&g_server.scene->tree);
	wlr_scene_node_set_enabled(&g_seat.drag.icons->node, false);

	g_seat.drag.events.request.notify = handle_drag_request;
	g_seat.drag.events.start.notify = handle_drag_start;
	g_seat.drag.events.destroy.notify = handle_drag_destroy;

	wl_signal_add(&g_seat.seat->events.request_start_drag,
		&g_seat.drag.events.request);
	wl_signal_add(&g_seat.seat->events.start_drag,
		&g_seat.drag.events.start);
	/*
	 * destroy.notify is listened to in handle_drag_start() and reset in
	 * handle_drag_destroy()
	 */
}

void
dnd_icons_show(bool show)
{
	wlr_scene_node_set_enabled(&g_seat.drag.icons->node, show);
}

void
dnd_icons_move(double x, double y)
{
	wlr_scene_node_set_position(&g_seat.drag.icons->node, x, y);
}

void
dnd_finish(void)
{
	wlr_scene_node_destroy(&g_seat.drag.icons->node);
	wl_list_remove(&g_seat.drag.events.request.link);
	wl_list_remove(&g_seat.drag.events.start.link);
}
