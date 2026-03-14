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
	struct lab_xwm *xwm;

	struct wlr_surface *surface;
	struct wlr_addon surface_addon;

	XSurfaceProps props;
	bool override_redirect;
	bool has_net_wm_name;

	struct wl_listener surface_commit;
	struct wl_listener surface_map;
	struct wl_listener surface_unmap;

	/* ViewId or 0 if unmanaged */
	unsigned long view_id;
};

struct xwayland_surface_configure_event {
	struct wlr_box geom;
	uint16_t mask; // xcb_config_window_t
};

void xwayland_set_cursor(const uint8_t *pixels, uint32_t stride,
	uint32_t width, uint32_t height, int32_t hotspot_x, int32_t hotspot_y);

void xwayland_surface_read_properties(struct xwayland_surface *xsurface);

/**
 * Restack surface above sibling.
 * If sibling is None, then the surface is moved to the bottom
 * of the stack.
 */
void xwayland_surface_stack_above(struct xwayland_surface *surface,
	xcb_window_t sibling);

void xwayland_surface_configure(struct xwayland_surface *surface,
	struct wlr_box geom);

void xwayland_surface_close(struct xwayland_surface *surface);

/**
 * Get a struct xwayland_surface from a struct wlr_surface.
 *
 * If the surface hasn't been created by Xwayland or has no X11 window
 * associated, NULL is returned.
 */
struct xwayland_surface *xwayland_surface_try_from_wlr_surface(
	struct wlr_surface *surface);

/**
 * Offer focus by sending WM_TAKE_FOCUS to a client window supporting it.
 * The client may accept or ignore the offer. If it accepts, the surface will
 * emit the focus_in signal notifying the compositor that it has received focus.
 *
 * This is a more compatible method of giving focus to windows using the
 * Globally Active input model (see xwayland_icccm_input_model()) than
 * calling xwayland_surface_activate() unconditionally, since there is no
 * reliable way to know in advance whether these windows want to be focused.
 */
void xwayland_surface_offer_focus(struct xwayland_surface *xsurface);

bool xwayland_surface_is_focused(struct xwayland_surface *xsurface);

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
bool xwayland_surface_fetch_icon(
	const struct xwayland_surface *xsurface,
	xcb_ewmh_get_wm_icon_reply_t *icon_reply);

/* listeners (external) */
void xwayland_on_ready(void);

void xwayland_surface_on_commit(struct xwayland_surface *xsurface);
void xwayland_surface_on_map(struct xwayland_surface *xsurface);
void xwayland_surface_on_unmap(struct xwayland_surface *xsurface);
void xwayland_surface_on_request_configure(struct xwayland_surface *xsurface,
	const struct xwayland_surface_configure_event *event);
void xwayland_surface_on_request_move(struct xwayland_surface *xsurface);
void xwayland_surface_on_request_resize(struct xwayland_surface *xsurface, uint32_t edges);
void xwayland_surface_on_set_icon(struct xwayland_surface *xsurface);
void xwayland_surface_on_set_override_redirect(struct xwayland_surface *xsurface);

#endif
