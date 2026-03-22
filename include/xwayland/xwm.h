// SPDX-License-Identifier: GPL-2.0-only
// adapted from wlroots (copyrights apply)
//
#ifndef XWAYLAND_XWM_H
#define XWAYLAND_XWM_H

#include <wayland-server-core.h>
#include <wlr/config.h>
#include <xcb/render.h>
#include <xcb/xcb_errors.h>
#include "xwayland/selection.h"
#include "xwayland/xwayland.h"
/* This is in xcb/xcb_event.h, but pulling xcb-util just for a constant
 * others redefine anyway is meh
 */
#define XCB_EVENT_RESPONSE_TYPE_MASK (0x7f)

enum atom_name {
	WL_SURFACE_ID,
	WL_SURFACE_SERIAL,
	WM_DELETE_WINDOW,
	WM_PROTOCOLS,
	WM_HINTS,
	WM_NORMAL_HINTS,
	WM_SIZE_HINTS,
	MOTIF_WM_HINTS,
	UTF8_STRING,
	WM_S0,
	NET_SUPPORTED,
	NET_WM_CM_S0,
	NET_WM_PID,
	NET_WM_NAME,
	NET_WM_STATE,
	NET_WM_STRUT_PARTIAL,
	NET_WM_WINDOW_TYPE,
	NET_WM_ICON,
	WM_TAKE_FOCUS,
	WINDOW,
	NET_ACTIVE_WINDOW,
	NET_CLOSE_WINDOW,
	NET_WM_MOVERESIZE,
	NET_SUPPORTING_WM_CHECK,
	NET_WM_STATE_FOCUSED,
	NET_WM_STATE_MODAL,
	NET_WM_STATE_FULLSCREEN,
	NET_WM_STATE_MAXIMIZED_VERT,
	NET_WM_STATE_MAXIMIZED_HORZ,
	NET_WM_STATE_HIDDEN,
	NET_WM_STATE_ABOVE,
	NET_WM_PING,
	WM_CHANGE_STATE,
	WM_STATE,
	CLIPBOARD,
	PRIMARY,
	WL_SELECTION,
	TARGETS,
	CLIPBOARD_MANAGER,
	INCR,
	TEXT,
	TIMESTAMP,
	DELETE,
	NET_WM_WINDOW_TYPE_NORMAL,
	NET_WM_WINDOW_TYPE_DIALOG,
	DND_SELECTION,
	DND_AWARE,
	DND_STATUS,
	DND_POSITION,
	DND_ENTER,
	DND_LEAVE,
	DND_DROP,
	DND_FINISHED,
	DND_PROXY,
	DND_TYPE_LIST,
	DND_ACTION_MOVE,
	DND_ACTION_COPY,
	DND_ACTION_ASK,
	DND_ACTION_PRIVATE,
	NET_CLIENT_LIST,
	NET_WORKAREA,
	ATOM_LAST // keep last
};

struct lab_xwm {
	struct wl_event_source *event_source;
	struct wlr_seat *seat;

	xcb_atom_t atoms[ATOM_LAST];
	xcb_connection_t *xcb_conn;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_render_pictformat_t render_format_id;
	xcb_cursor_t cursor;

	struct lab_xwm_selection clipboard_selection;
	struct lab_xwm_selection primary_selection;
	struct lab_xwm_selection dnd_selection;

	struct wlr_drag *drag;
	xcb_window_t drag_focus;
	xcb_window_t drop_focus;

	const xcb_query_extension_reply_t *xfixes;
	uint32_t xfixes_major_version;
	xcb_errors_context_t *errors_context;
	unsigned int last_focus_seq;

	struct wl_listener compositor_new_surface;
	struct wl_listener compositor_destroy;
	struct wl_listener shell_v1_new_surface;
	struct wl_listener shell_v1_destroy;
	struct wl_listener seat_set_selection;
	struct wl_listener seat_set_primary_selection;
	struct wl_listener seat_start_drag;
	struct wl_listener seat_drag_focus;
	struct wl_listener seat_drag_motion;
	struct wl_listener seat_drag_drop;
	struct wl_listener seat_drag_destroy;
	struct wl_listener seat_drag_source_destroy;
};

extern struct lab_xwm g_xwm;

// lab_xwm_create takes ownership of wm_fd and will close it under all circumstances.
bool lab_xwm_create(int wm_fd);

void lab_xwm_destroy(void);

void lab_xwm_set_cursor(struct lab_xwm *xwm, struct wlr_buffer *buffer,
	int32_t hotspot_x, int32_t hotspot_y);

int lab_xwm_handle_selection_event(struct lab_xwm *xwm, xcb_generic_event_t *event);
int lab_xwm_handle_selection_client_message(struct lab_xwm *xwm,
	xcb_client_message_event_t *ev);
void lab_xwm_seat_unlink_drag_handlers(struct lab_xwm *xwm);

void lab_xwm_set_seat(struct wlr_seat *seat);

char *lab_xwm_get_atom_name(struct lab_xwm *xwm, xcb_atom_t atom);
bool lab_xwm_atoms_contains(struct lab_xwm *xwm, xcb_atom_t *atoms,
	size_t num_atoms, enum atom_name needle);

xcb_void_cookie_t lab_xwm_send_event_with_size(xcb_connection_t *c,
	uint8_t propagate, xcb_window_t destination,
	uint32_t event_mask, const void *event, uint32_t length);

#endif
