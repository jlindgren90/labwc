/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef XWAYLAND_SHELL_H
#define XWAYLAND_SHELL_H

#include <stdbool.h>
#include <wayland-server-core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The Xwayland shell.
 *
 * This is a shell only exposed to Xwayland.
 */
struct xwayland_shell_v1 {
	struct wl_global *global;

	struct {
		struct wl_signal destroy;
		struct wl_signal new_surface; // struct xwayland_surface_v1
	} events;

	struct {
		struct wl_client *client;
		struct wl_list surfaces; // xwayland_surface_v1.link

		struct wl_listener display_destroy;
		struct wl_listener client_destroy;
	};
};

/**
 * An Xwayland shell surface.
 */
struct xwayland_surface_v1 {
	struct wlr_surface *surface;
	uint64_t serial;

	struct {
		struct wl_resource *resource;
		struct wl_list link;
		struct xwayland_shell_v1 *shell;
		bool added;
	};
};

/**
 * Create the xwayland_shell_v1 global.
 *
 * Compositors should add a global filter (see wl_display_set_global_filter())
 * to only expose this global to Xwayland clients.
 */
struct xwayland_shell_v1 *xwayland_shell_v1_create(
	struct wl_display *display, uint32_t version);

/**
 * Destroy the xwayland_shell_v1 global.
 */
void xwayland_shell_v1_destroy(struct xwayland_shell_v1 *shell);

/**
 * Allow a client to bind to the xwayland_shell_v1 global.
 */
void xwayland_shell_v1_set_client(struct xwayland_shell_v1 *shell,
	struct wl_client *client);

/**
 * Get a Wayland surface from an xwayland_shell_v1 serial.
 *
 * Returns NULL if the serial hasn't been associated with any surface.
 */
struct wlr_surface *xwayland_shell_v1_surface_from_serial(
	struct xwayland_shell_v1 *shell, uint64_t serial);

#ifdef __cplusplus
}
#endif

#endif
