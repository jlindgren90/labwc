/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef XWAYLAND_SERVER_H
#define XWAYLAND_SERVER_H

#include <stdbool.h>
#include <sys/types.h>
#include <time.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xwayland_server_options {
	bool lazy;
	bool enable_wm;
	bool no_touch_pointer_emulation;
	bool force_xrandr_emulation;
	int terminate_delay; // in seconds, 0 to terminate immediately
};

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
	struct xwayland_server_options options;

	struct wl_display *wl_display;
	struct wl_event_source *idle_source;

	struct {
		struct wl_signal start;
		struct wl_signal ready;
		struct wl_signal destroy;
	} events;

	void *data;

	struct {
		struct wl_listener client_destroy;
		struct wl_listener display_destroy;
	};
};

struct xwayland_server_ready_event {
	struct xwayland_server *server;
	int wm_fd;
};

struct xwayland_server *xwayland_server_create(
	struct wl_display *display, struct xwayland_server_options *options);
void xwayland_server_destroy(struct xwayland_server *server);

#ifdef __cplusplus
}
#endif

#endif
