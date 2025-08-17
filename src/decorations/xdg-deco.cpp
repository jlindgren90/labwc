// SPDX-License-Identifier: GPL-2.0-only
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include "config/rcxml.h"
#include "decorations.h"
#include "labwc.h"
#include "view.h"

struct xdg_deco : public destroyable {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_xdg_decoration;
	enum wlr_xdg_toplevel_decoration_v1_mode client_mode;
	struct view *view;

	DECLARE_HANDLER(xdg_deco, request_mode);
	DECLARE_HANDLER(xdg_deco, commit);
};

void
xdg_deco::handle_commit(void *)
{
	if (wlr_xdg_decoration->toplevel->base->initial_commit) {
		wlr_xdg_toplevel_decoration_v1_set_mode(wlr_xdg_decoration,
			client_mode);
		on_commit.disconnect();
	}
}

void
xdg_deco::handle_request_mode(void *)
{
	auto xdg_deco = this;
	enum wlr_xdg_toplevel_decoration_v1_mode client_mode =
		xdg_deco->wlr_xdg_decoration->requested_mode;

	switch (client_mode) {
	case WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE:
		xdg_deco->view->ssd_preference = LAB_SSD_PREF_SERVER;
		break;
	case WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE:
		xdg_deco->view->ssd_preference = LAB_SSD_PREF_CLIENT;
		break;
	case WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_NONE:
		xdg_deco->view->ssd_preference = LAB_SSD_PREF_UNSPEC;
		client_mode = rc.xdg_shell_server_side_deco
			? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
			: WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
		break;
	default:
		wlr_log(WLR_ERROR, "Unspecified xdg decoration variant "
			"requested: %u", client_mode);
	}

	/*
	 * We may get multiple request_mode calls in an uninitialized state.
	 * Just update the last requested mode and only add the commit
	 * handler on the first uninitialized state call.
	 */
	xdg_deco->client_mode = client_mode;

	if (xdg_deco->wlr_xdg_decoration->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(xdg_deco->wlr_xdg_decoration,
			client_mode);
	} else {
		CONNECT_LISTENER(wlr_xdg_decoration->toplevel->base->surface,
			this, commit);
	}

	if (client_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE) {
		view_set_ssd_mode(xdg_deco->view, LAB_SSD_MODE_FULL);
	} else {
		view_set_ssd_mode(xdg_deco->view, LAB_SSD_MODE_NONE);
	}
}

static void
xdg_toplevel_decoration(struct wl_listener *listener, void *data)
{
	auto wlr_xdg_decoration = (wlr_xdg_toplevel_decoration_v1 *)data;
	struct wlr_xdg_surface *xdg_surface = wlr_xdg_decoration->toplevel->base;
	if (!xdg_surface || !xdg_surface->data) {
		wlr_log(WLR_ERROR,
			"Invalid surface supplied for xdg decorations");
		return;
	}

	auto xdg_deco = new ::xdg_deco();
	xdg_deco->wlr_xdg_decoration = wlr_xdg_decoration;
	xdg_deco->view = (struct view *)xdg_surface->data;

	CONNECT_LISTENER(wlr_xdg_decoration, xdg_deco, destroy);
	CONNECT_LISTENER(wlr_xdg_decoration, xdg_deco, request_mode);

	xdg_deco->handle_request_mode();
}

void
xdg_server_decoration_init(void)
{
	struct wlr_xdg_decoration_manager_v1 *xdg_deco_mgr = NULL;
	xdg_deco_mgr =
		wlr_xdg_decoration_manager_v1_create(g_server.wl_display);
	if (!xdg_deco_mgr) {
		wlr_log(WLR_ERROR, "unable to create the XDG deco manager");
		exit(EXIT_FAILURE);
	}

	wl_signal_add(&xdg_deco_mgr->events.new_toplevel_decoration,
		&g_server.xdg_toplevel_decoration);
	g_server.xdg_toplevel_decoration.notify = xdg_toplevel_decoration;
}

void
xdg_server_decoration_finish(void)
{
	wl_list_remove(&g_server.xdg_toplevel_decoration.link);
}
