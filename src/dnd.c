// SPDX-License-Identifier: GPL-2.0-only
#include "dnd.h"
#include <assert.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "input/cursor.h"
#include "labwc.h"  /* for struct seat */

/* Internal DnD handlers */
static void
handle_drag_request(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(
			g_seat.wlr_seat, event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(g_seat.wlr_seat, event->drag,
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
	struct wlr_drag *drag = data;

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

	wl_signal_add(&g_seat.wlr_seat->events.request_start_drag,
		&g_seat.drag.events.request);
	wl_signal_add(&g_seat.wlr_seat->events.start_drag, &g_seat.drag.events.start);
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

void dnd_finish(void)
{
	wlr_scene_node_destroy(&g_seat.drag.icons->node);
	wl_list_remove(&g_seat.drag.events.request.link);
	wl_list_remove(&g_seat.drag.events.start.link);
}
