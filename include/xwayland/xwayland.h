// SPDX-License-Identifier: GPL-2.0-only
// adapted from wlroots (copyrights apply)
//
#ifndef XWAYLAND_XWAYLAND_H
#define XWAYLAND_XWAYLAND_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <wlr/util/addon.h>

struct wlr_box;
struct lab_xwm;
struct wlr_compositor;
struct wlr_data_source;
struct wlr_drag;
struct wlr_seat;

struct xwayland_server;

enum xwayland_surface_decorations {
	XWAYLAND_SURFACE_DECORATIONS_ALL = 0,
	XWAYLAND_SURFACE_DECORATIONS_NO_BORDER = 1,
	XWAYLAND_SURFACE_DECORATIONS_NO_TITLE = 2,
};

/**
 * This represents the input focus described as follows:
 *
 * https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#input_focus
 */
enum xwayland_icccm_input_model {
	WLR_ICCCM_INPUT_MODEL_NONE = 0,
	WLR_ICCCM_INPUT_MODEL_PASSIVE = 1,
	WLR_ICCCM_INPUT_MODEL_LOCAL = 2,
	WLR_ICCCM_INPUT_MODEL_GLOBAL = 3,
};

/**
 * The type of window (_NET_WM_WINDOW_TYPE). See:
 * https://specifications.freedesktop.org/wm-spec/latest/
 */
enum xwayland_net_wm_window_type {
	XWAYLAND_NET_WM_WINDOW_TYPE_DESKTOP = 0,
	XWAYLAND_NET_WM_WINDOW_TYPE_DOCK,
	XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR,
	XWAYLAND_NET_WM_WINDOW_TYPE_MENU,
	XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY,
	XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH,
	XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG,
	XWAYLAND_NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
	XWAYLAND_NET_WM_WINDOW_TYPE_POPUP_MENU,
	XWAYLAND_NET_WM_WINDOW_TYPE_TOOLTIP,
	XWAYLAND_NET_WM_WINDOW_TYPE_NOTIFICATION,
	XWAYLAND_NET_WM_WINDOW_TYPE_COMBO,
	XWAYLAND_NET_WM_WINDOW_TYPE_DND,
	XWAYLAND_NET_WM_WINDOW_TYPE_NORMAL,
};

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
	uint32_t surface_id;
	uint64_t serial;

	struct wl_list link;
	struct wl_list stack_link;
	struct wl_list unpaired_link;

	struct wlr_surface *surface;
	struct wlr_addon surface_addon;

	int16_t x, y;
	uint16_t width, height;
	bool override_redirect;

	char *title;
	char *class;
	char *instance;

	struct wl_list children; // xwayland_surface.parent_link
	struct xwayland_surface *parent;
	struct wl_list parent_link; // xwayland_surface.children

	xcb_atom_t *window_type;
	size_t window_type_len;

	xcb_atom_t *protocols;
	size_t protocols_len;

	uint32_t decorations;
	xcb_icccm_wm_hints_t *hints;
	xcb_size_hints_t *size_hints;
	/*
	 * _NET_WM_STRUT_PARTIAL (used by e.g. XWayland panels).
	 * Note that right/bottom values are offsets from the lower
	 * right corner of the X11 screen, and the exact relation
	 * between X11 screen coordinates and the wlr_output_layout
	 * depends on the XWayland implementation.
	 */
	xcb_ewmh_wm_strut_partial_t *strut_partial;

	// _NET_WM_STATE
	bool modal;
	bool fullscreen;
	bool maximized_vert, maximized_horz;
	bool minimized;
	bool withdrawn;
	bool above;

	struct {
		struct wl_signal destroy;
		struct wl_signal request_configure; // struct xwayland_surface_configure_event
		struct wl_signal request_move;
		struct wl_signal request_resize; // struct xwayland_resize_event
		struct wl_signal request_minimize; // struct xwayland_minimize_event
		struct wl_signal request_maximize;
		struct wl_signal request_fullscreen;
		struct wl_signal request_activate;
		struct wl_signal request_close;
		struct wl_signal request_above;

		struct wl_signal set_title;
		struct wl_signal set_override_redirect;
	} events;

	struct {
		char *wm_name, *net_wm_name;

		struct wl_listener surface_commit;
		struct wl_listener surface_map;
		struct wl_listener surface_unmap;
	};

	/* ViewId or 0 if unmanaged */
	unsigned long view_id;
	bool focused_before_map;

	/* for unmanaged surfaces */
	bool ever_grabbed_focus;
	struct wlr_scene_node *unmanaged_node;
};

struct xwayland_surface_configure_event {
	struct xwayland_surface *surface;
	int16_t x, y;
	uint16_t width, height;
	uint16_t mask; // xcb_config_window_t
};

struct xwayland_resize_event {
	struct xwayland_surface *surface;
	uint32_t edges;
};

struct xwayland_minimize_event {
	struct xwayland_surface *surface;
	bool minimize;
};

void xwayland_set_cursor(struct xwayland_server *server, const uint8_t *pixels,
	uint32_t stride, uint32_t width, uint32_t height, int32_t hotspot_x,
	int32_t hotspot_y);

void xwayland_surface_activate(struct xwayland_surface *surface,
	bool activated);

/**
 * Restack surface relative to sibling.
 * If sibling is NULL, then the surface is moved to the top or the bottom
 * of the stack (depending on the mode).
 */
void xwayland_surface_restack(struct xwayland_surface *surface,
	struct xwayland_surface *sibling, enum xcb_stack_mode_t mode);

void xwayland_surface_configure(struct xwayland_surface *surface,
	int16_t x, int16_t y, uint16_t width, uint16_t height);

void xwayland_surface_close(struct xwayland_surface *surface);

void xwayland_surface_set_withdrawn(struct xwayland_surface *surface,
	bool withdrawn);

void xwayland_surface_set_minimized(struct xwayland_surface *surface,
	bool minimized);

void xwayland_surface_set_maximized(struct xwayland_surface *surface,
	bool maximized_horz, bool maximized_vert);

void xwayland_surface_set_fullscreen(struct xwayland_surface *surface,
	bool fullscreen);

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

/**
 * Returns true if the surface has the given window type.
 * Note: a surface may have multiple window types set.
 */
bool xwayland_surface_has_window_type(
	const struct xwayland_surface *xsurface,
	enum xwayland_net_wm_window_type window_type);

enum xwayland_icccm_input_model xwayland_surface_icccm_input_model(
	const struct xwayland_surface *xsurface);

/**
 * Sets the _NET_WORKAREA root window property. The compositor should set
 * one workarea per virtual desktop. This indicates the usable geometry
 * (relative to the virtual desktop viewport) that is not covered by
 * panels, docks, etc. Unfortunately, it is not possible to specify
 * per-output workareas.
 */
void xwayland_set_workareas(struct xwayland_server *server,
	const struct wlr_box *workareas, size_t num_workareas);

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
void xwayland_on_new_surface(struct xwayland_surface *xsurface);
void xwayland_on_ready(void);

void xwayland_surface_on_map_request(struct xwayland_surface *xsurface);
void xwayland_surface_on_commit(struct xwayland_surface *xsurface);
void xwayland_surface_on_map(struct xwayland_surface *xsurface);
void xwayland_surface_on_unmap(struct xwayland_surface *xsurface);
void xwayland_surface_on_set_geometry(struct xwayland_surface *xsurface);
void xwayland_surface_on_set_class(struct xwayland_surface *xsurface);
void xwayland_surface_on_set_decorations(struct xwayland_surface *xsurface);
void xwayland_surface_on_set_icon(struct xwayland_surface *xsurface);
void xwayland_surface_on_set_strut_partial(struct xwayland_surface *xsurface);
void xwayland_surface_on_focus_in(struct xwayland_surface *xsurface);
void xwayland_surface_on_grab_focus(struct xwayland_surface *xsurface);

#endif
