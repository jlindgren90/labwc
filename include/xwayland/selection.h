#ifndef XWAYLAND_SELECTION_H
#define XWAYLAND_SELECTION_H

#include <stdbool.h>
#include <xcb/xfixes.h>
#include <wayland-util.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INCR_CHUNK_SIZE (64 * 1024)

#define XDND_VERSION 5

struct wlr_primary_selection_source;

struct xwm_selection;

struct wlr_drag;
struct wlr_data_source;

struct xwm_selection_transfer {
	struct xwm_selection *selection;

	bool incr;
	bool flush_property_on_delete;
	bool property_set;
	struct wl_array source_data;
	int wl_client_fd;
	struct wl_event_source *event_source;
	struct wl_list link;

	// when sending to x11
	xcb_selection_request_event_t request;

	// when receiving from x11
	int property_start;
	xcb_get_property_reply_t *property_reply;
	xcb_window_t incoming_window;
};

struct xwm_selection {
	struct xwm *xwm;

	xcb_atom_t atom;
	xcb_window_t window;
	xcb_window_t owner;
	xcb_timestamp_t timestamp;

	struct wl_list incoming;
	struct wl_list outgoing;
};

struct xwm_selection_transfer *
xwm_selection_find_incoming_transfer_by_window(
	struct xwm_selection *selection, xcb_window_t window);

void xwm_selection_transfer_remove_event_source(
	struct xwm_selection_transfer *transfer);
void xwm_selection_transfer_close_wl_client_fd(
	struct xwm_selection_transfer *transfer);
void xwm_selection_transfer_destroy_property_reply(
	struct xwm_selection_transfer *transfer);
void xwm_selection_transfer_init(struct xwm_selection_transfer *transfer,
	struct xwm_selection *selection);
void xwm_selection_transfer_destroy(
	struct xwm_selection_transfer *transfer);

void xwm_selection_transfer_destroy_outgoing(
	struct xwm_selection_transfer *transfer);

xcb_atom_t xwm_mime_type_to_atom(struct xwm *xwm, char *mime_type);
char *xwm_mime_type_from_atom(struct xwm *xwm, xcb_atom_t atom);
struct xwm_selection *xwm_get_selection(struct xwm *xwm,
	xcb_atom_t selection_atom);

void xwm_send_incr_chunk(struct xwm_selection_transfer *transfer);
void xwm_handle_selection_request(struct xwm *xwm,
	xcb_selection_request_event_t *req);
void xwm_handle_selection_destroy_notify(struct xwm *xwm,
		xcb_destroy_notify_event_t *event);

void xwm_get_incr_chunk(struct xwm_selection_transfer *transfer);
void xwm_handle_selection_notify(struct xwm *xwm,
	xcb_selection_notify_event_t *event);
int xwm_handle_xfixes_selection_notify(struct xwm *xwm,
	xcb_xfixes_selection_notify_event_t *event);
bool data_source_is_xwayland(struct wlr_data_source *wlr_source);
bool primary_selection_source_is_xwayland(
	struct wlr_primary_selection_source *wlr_source);

void xwm_seat_handle_start_drag(struct xwm *xwm, struct wlr_drag *drag);

void xwm_selection_init(struct xwm_selection *selection,
	struct xwm *xwm, xcb_atom_t atom);
void xwm_selection_finish(struct xwm_selection *selection);

#ifdef __cplusplus
}
#endif

#endif
