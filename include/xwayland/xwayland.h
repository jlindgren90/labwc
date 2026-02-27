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
struct wlr_data_source;
struct wlr_drag;

/**
 * Xwayland integration.
 *
 * This includes a utility to start and monitor the Xwayland process (see
 * struct xwayland_server), an implementation of the xwayland_shell_v1
 * Wayland protocol, and a X11 window manager.
 *
 * Compositors are expected to set DISPLAY (see display_name) and listen to the
 * new_surface event.
 *
 * Compositors may want to only expose the xwayland_shell_v1 Wayland global to
 * the Xwayland client. To do so, they can set up a global filter via
 * wl_display_set_global_filter() to ensure the global stored in
 * shell_v1.global is only exposed to the client stored in server.client.
 */
struct xwayland {
	struct xwayland_server *server;
	bool own_server;
	struct lab_xwm *xwm;
	struct xwayland_shell_v1 *shell_v1;
	struct xwayland_cursor *cursor;

	// Value the DISPLAY environment variable should be set to by the compositor
	const char *display_name;

	struct wl_display *wl_display;
	struct wlr_compositor *compositor;
	struct wlr_seat *seat;

	struct {
		struct wl_signal destroy;
		struct wl_signal ready;
		struct wl_signal new_surface; // struct xwayland_surface
		struct wl_signal remove_startup_info; // struct xwayland_remove_startup_info_event
	} events;

	/**
	 * Add a custom event handler to xwayland. Return true if the event was
	 * handled or false to use the default wlr-xwayland handler. wlr-xwayland will
	 * free the event.
	 */
	bool (*user_event_handler)(struct xwayland *xwayland, xcb_generic_event_t *event);

	void *data;

	struct {
		struct wl_listener server_start;
		struct wl_listener server_ready;
		struct wl_listener server_destroy;
		struct wl_listener seat_destroy;
		struct wl_listener shell_destroy;
	};
};

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
	float opacity;

	char *title;
	char *class;
	char *instance;
	char *role;
	char *startup_id;
	pid_t pid;
	bool has_utf8_title;

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

	bool pinging;
	struct wl_event_source *ping_timer;

	// _NET_WM_STATE
	bool modal;
	bool fullscreen;
	bool maximized_vert, maximized_horz;
	bool minimized;
	bool withdrawn;
	bool sticky;
	bool shaded;
	bool skip_taskbar;
	bool skip_pager;
	bool above;
	bool below;
	bool demands_attention;

	bool has_alpha;

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
		struct wl_signal request_sticky;
		struct wl_signal request_shaded;
		struct wl_signal request_skip_taskbar;
		struct wl_signal request_skip_pager;
		struct wl_signal request_above;
		struct wl_signal request_below;
		struct wl_signal request_demands_attention;

		struct wl_signal associate;
		struct wl_signal dissociate;

		struct wl_signal set_title;
		struct wl_signal set_class;
		struct wl_signal set_role;
		struct wl_signal set_parent;
		struct wl_signal set_startup_id;
		struct wl_signal set_window_type;
		struct wl_signal set_hints;
		struct wl_signal set_decorations;
		struct wl_signal set_strut_partial;
		struct wl_signal set_override_redirect;
		struct wl_signal set_geometry;
		struct wl_signal set_opacity;
		struct wl_signal set_icon;
		struct wl_signal focus_in;
		struct wl_signal grab_focus;
		/* can be used to set initial maximized/fullscreen geometry */
		struct wl_signal map_request;
		struct wl_signal ping_timeout;
	} events;

	void *data;

	struct {
		char *wm_name, *net_wm_name;

		struct wl_listener surface_commit;
		struct wl_listener surface_map;
		struct wl_listener surface_unmap;
	};
};

struct xwayland_surface_configure_event {
	struct xwayland_surface *surface;
	int16_t x, y;
	uint16_t width, height;
	uint16_t mask; // xcb_config_window_t
};

struct xwayland_remove_startup_info_event  {
	const char *id;
	xcb_window_t window;
};

struct xwayland_resize_event {
	struct xwayland_surface *surface;
	uint32_t edges;
};

struct xwayland_minimize_event {
	struct xwayland_surface *surface;
	bool minimize;
};

/** Create an Xwayland server and XWM.
 *
 * The server supports a lazy mode in which Xwayland is only started when a
 * client tries to connect.
 */
struct xwayland *xwayland_create(struct wl_display *wl_display,
	struct wlr_compositor *compositor, bool lazy);

/**
 * Create an XWM from an existing Xwayland server.
 */
struct xwayland *xwayland_create_with_server(struct wl_display *display,
	struct wlr_compositor *compositor, struct xwayland_server *server);

void xwayland_destroy(struct xwayland *xwayland);

void xwayland_set_cursor(struct xwayland *xwayland,
	uint8_t *pixels, uint32_t stride, uint32_t width, uint32_t height,
	int32_t hotspot_x, int32_t hotspot_y);

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

void xwayland_surface_set_sticky(
	struct xwayland_surface *surface, bool sticky);

void xwayland_surface_set_shaded(
	struct xwayland_surface *surface, bool shaded);

void xwayland_surface_set_skip_taskbar(
	struct xwayland_surface *surface, bool skip_taskbar);

void xwayland_surface_set_skip_pager(
	struct xwayland_surface *surface, bool skip_pager);

void xwayland_surface_set_above(
	struct xwayland_surface *surface, bool above);

void xwayland_surface_set_below(
	struct xwayland_surface *surface, bool below);

void xwayland_surface_set_demands_attention(
	struct xwayland_surface *surface, bool demands_attention);

void xwayland_set_seat(struct xwayland *xwayland,
	struct wlr_seat *seat);

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

void xwayland_surface_ping(struct xwayland_surface *surface);

/**
 * Returns true if the surface has the given window type.
 * Note: a surface may have multiple window types set.
 */
bool xwayland_surface_has_window_type(
	const struct xwayland_surface *xsurface,
	enum xwayland_net_wm_window_type window_type);

/** Metric to guess if an OR window should "receive" focus
 *
 * In the pure X setups, window managers usually straight up ignore override
 * redirect windows, and never touch them. (we have to handle them for mapping)
 *
 * When such a window wants to receive keyboard input (e.g. rofi/dzen) it will
 * use mechanics we don't support (sniffing/grabbing input).
 * [Sadly this is unrelated to xwayland-keyboard-grab]
 *
 * To still support these windows, while keeping general OR semantics as is, we
 * need to hand a subset of windows focus.
 * The dirty truth is, we need to hand focus to any Xwayland window, though
 * pretending this window has focus makes it easier to handle unmap.
 *
 * This function provides a handy metric based on the window type to guess if
 * the OR window wants focus.
 * It's probably not perfect, nor exactly intended but works in practice.
 *
 * Returns: true if the window should receive focus
 *          false if it should be ignored
 */
bool xwayland_surface_override_redirect_wants_focus(
	const struct xwayland_surface *xsurface);

enum xwayland_icccm_input_model xwayland_surface_icccm_input_model(
	const struct xwayland_surface *xsurface);

/**
 * Sets the _NET_WORKAREA root window property. The compositor should set
 * one workarea per virtual desktop. This indicates the usable geometry
 * (relative to the virtual desktop viewport) that is not covered by
 * panels, docks, etc. Unfortunately, it is not possible to specify
 * per-output workareas.
 */
void xwayland_set_workareas(struct xwayland *xwayland,
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

/**
 * Get the XCB connection of the XWM.
 *
 * The connection is only valid after xwayland.events.ready, and becomes
 * invalid on xwayland_server.events.destroy. In that case, NULL is
 * returned.
 */
xcb_connection_t *xwayland_get_xwm_connection(
	struct xwayland *xwayland);

#endif
