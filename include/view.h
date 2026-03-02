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
	struct xwayland_surface *xwayland_surface;

	/* Events unique to XWayland views */
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_above;
	struct wl_listener request_activate;
	struct wl_listener request_configure;
	struct wl_listener set_override_redirect;
};

ViewId view_from_wlr_surface(struct wlr_surface *surface);

struct wlr_surface *view_get_surface(struct view *view);

bool view_inhibits_actions(ViewId view_id, struct wl_list *actions);

void view_init(struct view *view, bool is_xwayland);
void view_destroy(struct view *view);

enum view_axis view_axis_parse(const char *direction);

#endif /* LABWC_VIEW_H */
