// SPDX-License-Identifier: GPL-2.0-only
// adapted from wlroots (copyrights apply)
//
#ifndef XWAYLAND_XWAYLAND_H
#define XWAYLAND_XWAYLAND_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/util/addon.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include "view.h"

struct wlr_box;
struct wlr_buffer;
struct lab_xwm;
struct wlr_compositor;
struct wlr_data_source;
struct wlr_drag;
struct wlr_seat;

struct xwayland_server;

/**
 * An Xwayland user interface component. It has an absolute position in
 * layout-local coordinates.
 *
 * The inner struct wlr_surface is valid once the associate event is emitted.
 * Compositors can set up e.g. map and unmap listeners at this point. The
 * struct wlr_surface becomes invalid when the dissociate event is emitted.
 */
struct xwayland_surface {
	xcb_window_t window_id;

	struct wlr_surface *surface;
	struct wlr_addon surface_addon;

	struct wl_listener surface_commit;
	struct wl_listener surface_map;
	struct wl_listener surface_unmap;

	/* ViewId or 0 if unmanaged */
	unsigned long view_id;
};

void xwayland_set_cursor(struct wlr_buffer *buffer,
	int32_t hotspot_x, int32_t hotspot_y);

/**
 * Get a struct xwayland_surface from a struct wlr_surface.
 *
 * If the surface hasn't been created by Xwayland or has no X11 window
 * associated, NULL is returned.
 */
struct xwayland_surface *xwayland_surface_try_from_wlr_surface(
	struct wlr_surface *surface);

/**
 * Sets the _NET_WORKAREA root window property. The compositor should set
 * one workarea per virtual desktop. This indicates the usable geometry
 * (relative to the virtual desktop viewport) that is not covered by
 * panels, docks, etc. Unfortunately, it is not possible to specify
 * per-output workareas.
 */
void xwayland_set_workareas(const struct wlr_box *workareas, size_t num_workareas);

/**
 * Fetches the icon set via the _NET_WM_ICON property.
 *
 * Returns true on success. The caller is responsible for freeing the reply
 * using xcb_ewmh_get_wm_icon_reply_wipe().
 */
bool xwayland_fetch_window_icon(xcb_window_t window_id,
	xcb_ewmh_get_wm_icon_reply_t *icon_reply);

/* listeners (external) */
void xwayland_on_ready(void);

void xwayland_on_set_window_icon(xcb_window_t window_id);

#endif
