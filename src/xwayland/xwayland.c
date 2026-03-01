// SPDX-License-Identifier: GPL-2.0-only
// adapted from wlroots (copyrights apply)
//
#define _POSIX_C_SOURCE 200809L
#include "xwayland/xwayland.h"
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "xwayland/server.h"
#include "xwayland/shell.h"
#include "xwayland/xwm.h"

void
xwayland_on_server_start(struct xwayland *xwayland)
{
	if (xwayland->shell_v1 != NULL) {
		xwayland_shell_v1_set_client(xwayland->shell_v1, xwayland->server->client);
	}
}

void
xwayland_on_server_ready(struct xwayland *xwayland)
{
	assert(xwayland->server->wm_fd[0] >= 0);
	xwayland->xwm = lab_xwm_create(xwayland, xwayland->server->wm_fd[0]);
	// lab_xwm_create takes ownership of wm_fd[0] under all circumstances
	xwayland->server->wm_fd[0] = -1;

	if (!xwayland->xwm) {
		return;
	}

	if (xwayland->seat) {
		lab_xwm_set_seat(xwayland->xwm, xwayland->seat);
	}

	xwayland_on_ready();
}

static void handle_shell_destroy(struct wl_listener *listener, void *data) {
	struct xwayland *xwayland =
		wl_container_of(listener, xwayland, shell_destroy);
	xwayland->shell_v1 = NULL;
	wl_list_remove(&xwayland->shell_destroy.link);
	// Will remove this list in handle_shell_destroy().
	// This ensures the link is always initialized and
	// avoids the need to keep check conditions in sync.
	wl_list_init(&xwayland->shell_destroy.link);
}

void xwayland_destroy(struct xwayland *xwayland) {
	if (!xwayland) {
		return;
	}

	wl_list_remove(&xwayland->shell_destroy.link);

	xwayland_set_seat(xwayland, NULL);
	xwayland_server_destroy(xwayland->server);
	xwayland->server = NULL;
	xwayland_shell_v1_destroy(xwayland->shell_v1);
	lab_xwm_destroy(xwayland->xwm);
	free(xwayland);
}

static struct xwayland *
xwayland_create_inner(struct wl_display *wl_display,
		struct wlr_compositor *compositor)
{
	struct xwayland *xwayland = calloc(1, sizeof(*xwayland));
	if (!xwayland) {
		return NULL;
	}

	struct xwayland_server *server =
		xwayland_server_create(wl_display, xwayland);
	if (server == NULL) {
		free(xwayland);
		return NULL;
	}

	xwayland->wl_display = wl_display;
	xwayland->compositor = compositor;

	xwayland->server = server;
	xwayland->display_name = xwayland->server->display_name;

	wl_list_init(&xwayland->shell_destroy.link);

	return xwayland;
}

struct xwayland *
xwayland_create(struct wl_display *wl_display, struct wlr_compositor *compositor)
{
	struct xwayland_shell_v1 *shell_v1 = xwayland_shell_v1_create(wl_display, 1);
	if (shell_v1 == NULL) {
		return NULL;
	}

	struct xwayland *xwayland = xwayland_create_inner(wl_display, compositor);
	if (xwayland == NULL) {
		xwayland_shell_v1_destroy(shell_v1);
		return NULL;
	}

	xwayland->shell_v1 = shell_v1;

	xwayland->shell_destroy.notify = handle_shell_destroy;
	wl_signal_add(&xwayland->shell_v1->events.destroy, &xwayland->shell_destroy);

	return xwayland;
}

void xwayland_set_cursor(struct xwayland *xwayland,
		uint8_t *pixels, uint32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y) {
	if (xwayland->xwm != NULL) {
		lab_xwm_set_cursor(xwayland->xwm, pixels, stride, width, height,
			hotspot_x, hotspot_y);
		return;
	}
}

static void xwayland_handle_seat_destroy(struct wl_listener *listener,
		void *data) {
	struct xwayland *xwayland =
		wl_container_of(listener, xwayland, seat_destroy);

	xwayland_set_seat(xwayland, NULL);
}

void xwayland_set_seat(struct xwayland *xwayland,
		struct wlr_seat *seat) {
	if (xwayland->seat) {
		wl_list_remove(&xwayland->seat_destroy.link);
	}

	xwayland->seat = seat;

	if (xwayland->xwm) {
		lab_xwm_set_seat(xwayland->xwm, seat);
	}

	if (seat == NULL) {
		return;
	}

	xwayland->seat_destroy.notify = xwayland_handle_seat_destroy;
	wl_signal_add(&seat->events.destroy, &xwayland->seat_destroy);
}
