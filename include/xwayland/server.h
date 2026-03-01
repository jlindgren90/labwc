// SPDX-License-Identifier: GPL-2.0-only
// adapted from wlroots (copyrights apply)
//
#ifndef XWAYLAND_SERVER_H
#define XWAYLAND_SERVER_H

#include <stdbool.h>
#include <sys/types.h>
#include <time.h>
#include <wayland-server-core.h>

struct xwayland_server {
	pid_t pid;
	struct wl_client *client;
	struct wl_event_source *pipe_source;
	int wm_fd[2], wl_fd[2];
	bool ready;

	time_t server_start;

	/* Anything above display is reset on Xwayland restart, rest is conserved */

	int display;
	char display_name[16];
	int x_fd[2];
	struct wl_event_source *x_fd_read_event[2];

	struct wl_display *wl_display;
	struct wl_event_source *idle_source;

	struct wl_listener client_destroy;

	struct lab_xwm *xwm;
	struct xwayland_shell_v1 *shell_v1;

	struct wlr_compositor *compositor;
	struct wlr_seat *seat;
};

struct xwayland_server *xwayland_server_create(struct wl_display *display,
	struct wlr_compositor *compositor, struct wlr_seat *seat);

void xwayland_server_destroy(struct xwayland_server *server);

#endif
