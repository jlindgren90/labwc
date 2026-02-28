/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_H
#define LABWC_H

#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "common/set.h"
#include "cycle.h"
#include "input/cursor.h"

#define XCURSOR_DEFAULT "left_ptr"
#define XCURSOR_SIZE 24

struct output;
struct wlr_xdg_popup;
struct wlr_xdg_surface;

enum input_mode {
	LAB_INPUT_STATE_PASSTHROUGH = 0,
	LAB_INPUT_STATE_MOVE,
	LAB_INPUT_STATE_RESIZE,
	LAB_INPUT_STATE_MENU,
	LAB_INPUT_STATE_CYCLE, /* a.k.a. window switching */
};

struct seat {
	struct wlr_seat *wlr_seat;
	struct wlr_keyboard_group *keyboard_group;

	/*
	 * Enum of most recent server-side cursor image.  Set by
	 * cursor_set().  Cleared when a client surface is entered
	 * (in that case the client is expected to set its own cursor image).
	 */
	enum lab_cursors server_cursor;
	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *xcursor_manager;
	struct accumulated_scroll {
		double delta;
		double delta_discrete;
	} accumulated_scrolls[2]; /* indexed by wl_pointer_axis */

	/*
	 * The surface whose keyboard focus is temporarily cleared with
	 * seat_focus_override_begin() and restored with
	 * seat_focus_override_end().
	 */
	struct {
		struct wlr_surface *surface;
		struct wl_listener surface_destroy;
	} focus_override;

	/* if set, views cannot receive focus */
	struct wlr_layer_surface_v1 *focused_layer;

	/**
	 * Cursor context saved when a mouse button is pressed on a view/surface.
	 * It is used to send cursor motion events to a surface even though
	 * the cursor has left the surface in the meantime.
	 *
	 * This allows to keep dragging a scrollbar or selecting text even
	 * when moving outside of the window.
	 *
	 * It is also used to:
	 * - determine the target view for action in "Drag" mousebind
	 * - validate view move/resize requests from CSD clients
	 */
	struct cursor_context_saved pressed;

	/* Cursor context of the last cursor motion */
	struct cursor_context_saved last_cursor_ctx;

	struct lab_set bound_buttons;

	struct {
		bool active;
		struct {
			struct wl_listener request;
			struct wl_listener start;
			struct wl_listener destroy;
		} events;
		struct wlr_scene_tree *icons;
	} drag;

	struct wl_list inputs;
	struct wl_listener new_input;
	struct wl_listener focus_change;

	struct {
		struct wl_listener motion;
		struct wl_listener motion_absolute;
		struct wl_listener button;
		struct wl_listener axis;
		struct wl_listener frame;
	} on_cursor;

	struct wl_listener request_set_cursor;
	struct wl_listener request_set_shape;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;
};

struct server {
	struct wl_display *wl_display;
	struct wl_event_loop *wl_event_loop;  /* Can be used for timer events */
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_backend *backend;
	struct headless {
		struct wlr_backend *backend;
	} headless;
	struct wlr_session *session;
	struct wlr_linux_dmabuf_v1 *linux_dmabuf;
	struct wlr_compositor *compositor;

	struct wl_event_source *sighup_source;
	struct wl_event_source *sigint_source;
	struct wl_event_source *sigterm_source;
	struct wl_event_source *sigchld_source;

	struct wlr_xdg_shell *xdg_shell;
	struct wlr_layer_shell_v1 *layer_shell;

	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_layer_surface;

	struct xwayland *xwayland;
	struct wl_listener xwayland_server_ready;
	struct wl_listener xwayland_xwm_ready;
	struct wl_listener xwayland_new_surface;

	struct wlr_xdg_activation_v1 *xdg_activation;
	struct wl_listener xdg_activation_request;
	struct wl_listener xdg_activation_new_token;

	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	bool direct_scanout_enabled;

	/* cursor interactive */
	enum input_mode input_mode;

	struct ssd_button *hovered_button;

	/* Tree for all non-layer xdg/xwayland-shell surfaces */
	struct wlr_scene_tree *view_tree;
	/* Tree for unmanaged xsurfaces without initialized view (usually popups) */
	struct wlr_scene_tree *unmanaged_tree;
	/* Tree for built in menu */
	struct wlr_scene_tree *menu_tree;

	struct wl_list outputs;
	struct wl_listener new_output;
	struct wlr_output_layout *output_layout;
	float max_output_scale;

	struct wl_listener output_layout_change;
	struct wlr_output_manager_v1 *output_manager;
	struct wl_listener output_manager_test;
	struct wl_listener output_manager_apply;
	/*
	 * While an output layout change is in process, this counter is
	 * non-zero and causes change-events from the wlr_output_layout
	 * to be ignored (to prevent, for example, moving views in a
	 * transitory layout state).  Once the counter reaches zero,
	 * do_output_layout_change() must be called explicitly.
	 */
	int pending_output_layout_change;

	struct wl_listener renderer_lost;

	struct wlr_gamma_control_manager_v1 *gamma_control_manager_v1;
	struct wl_listener gamma_control_set_gamma;

	struct session_lock_manager *session_lock_manager;

	struct wlr_drm_lease_v1_manager *drm_lease_manager;
	struct wl_listener drm_lease_request;

	struct wlr_output_power_manager_v1 *output_power_manager_v1;
	struct wl_listener output_power_manager_set_mode;

	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

	/* Set when in cycle (alt-tab) mode */
	struct cycle_state cycle;

	struct menu *menu_current;
	struct wl_list menus;

	struct sfdo *sfdo;

	pid_t primary_client_pid;
};

/*
 * Globals
 *
 * Rationale: these are unlikely to ever have more than one instance
 * per process, and need to last for the lifetime of the process.
 * Accessing them indirectly through pointers embedded in every other
 * struct just adds noise to the code.
 */
extern struct seat g_seat;
extern struct server g_server;

void xdg_popup_create(ViewId view_id, struct wlr_xdg_surface *toplevel,
	struct wlr_xdg_popup *wlr_popup, struct wlr_scene_tree *parent_tree);
void xdg_shell_init(void);
void xdg_shell_finish(void);

/*
 * desktop.c routines deal with a collection of views
 *
 * Definition of a few keywords used in desktop.c
 *   raise    - Bring view to front.
 *   focus    - Give keyboard focus to view.
 *   activate - Set view surface as active so that client window decorations
 *              are painted to show that the window is active,typically by
 *              using a different color. Although xdg-shell protocol says you
 *              cannot assume this means that the window actually has keyboard
 *              or pointer focus, in this compositor are they called together.
 */

/**
 * desktop_focus_view_or_surface() - like desktop_focus_view() but can
 * also focus other (e.g. xwayland-unmanaged) surfaces
 */
void desktop_focus_view_or_surface(ViewId view_id,
	struct wlr_surface *surface, bool raise);

void seat_init(void);
void seat_finish(void);
void seat_reconfigure(void);
void seat_force_focus_surface(struct wlr_surface *surface);
void seat_focus_surface(struct wlr_surface *surface);
void seat_focus_surface_no_notify(struct wlr_surface *surface);

void seat_pointer_end_grab(struct wlr_surface *surface);

/**
 * seat_focus_lock_surface() - ONLY to be called from session-lock.c to
 * focus lock screen surfaces. Use seat_focus_surface() otherwise.
 */
void seat_focus_lock_surface(struct wlr_surface *surface);

void seat_set_focus_layer(struct wlr_layer_surface_v1 *layer);
void seat_output_layout_changed(void);

/*
 * Temporarily clear the pointer/keyboard focus from the client at the
 * beginning of interactive move/resize, window switcher or menu interactions.
 * The focus is kept cleared until seat_focus_override_end() is called or
 * layer-shell/session-lock surfaces are mapped.
 */
void seat_focus_override_begin(enum input_mode input_mode,
	enum lab_cursors cursor_shape);
/*
 * If restore_focus=true, restore the pointer/keyboard focus which was cleared
 * in seat_focus_override_begin().
 */
void seat_focus_override_end(bool restore_focus);

void interactive_set_grab_context(struct cursor_context *ctx);
void interactive_begin(ViewId view_id, enum input_mode mode,
	enum lab_edge edges);

void server_init(void);
void server_start(void);
void server_finish(void);

#endif /* LABWC_H */
