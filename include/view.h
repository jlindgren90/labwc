/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_VIEW_H
#define LABWC_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>
#include <wlr/util/box.h>
#include <xkbcommon/xkbcommon.h>

#define Rect struct wlr_box
#include "view-c.h"
#include "view_api.h"

struct wlr_surface;

struct view {
	/* rust interop */
	ViewId id;
	const ViewState *st;

	/* used by xdg-shell views */
	uint32_t pending_configure_serial;
	struct wl_event_source *pending_configure_timeout;

	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_minimize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;

	/* xdg-shell view fields */
	struct wlr_xdg_surface *xdg_surface;

	/* Events unique to xdg-toplevel views */
	struct wl_listener set_app_id;
	struct wl_listener request_show_window_menu;
	struct wl_listener new_popup;

	/* xwayland view fields */
	struct wlr_xwayland_surface *xwayland_surface;
	bool focused_before_map;

	/* Events unique to XWayland views */
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_above;
	struct wl_listener request_activate;
	struct wl_listener request_configure;
	struct wl_listener set_class;
	struct wl_listener set_decorations;
	struct wl_listener set_override_redirect;
	struct wl_listener set_strut_partial;
	struct wl_listener set_window_type;
	struct wl_listener set_icon;
	struct wl_listener focus_in;
	struct wl_listener map_request;

	/* xwayland-unmanaged fields */
	struct wlr_scene_node *node;
	struct wl_list link;

	struct wl_listener grab_focus;
	struct wl_listener set_geometry;

	/*
	 * True if the surface has performed a keyboard grab. labwc
	 * honors keyboard grabs and will give the surface focus when
	 * it's mapped (which may occur slightly later) and on top.
	 */
	bool ever_grabbed_focus;
};

ViewId view_from_wlr_surface(struct wlr_surface *surface);

struct wlr_surface *view_get_surface(struct view *view);

bool view_inhibits_actions(ViewId view_id, struct wl_list *actions);

void view_init(struct view *view, bool is_xwayland);
void view_destroy(struct view *view);

enum view_axis view_axis_parse(const char *direction);

#endif /* LABWC_VIEW_H */
