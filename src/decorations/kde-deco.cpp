// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "config/rcxml.h"
#include "decorations.h"
#include "labwc.h"
#include "view.h"

static reflist<struct kde_deco> decorations;
static struct wlr_server_decoration_manager *kde_deco_mgr;

struct kde_deco : public destroyable, public ref_guarded<kde_deco> {
	struct wlr_server_decoration *wlr_kde_decoration;
	struct view *view;

	~kde_deco() { decorations.remove(this); }

	void handle_mode(void * = nullptr);

	DECLARE_LISTENER(kde_deco, mode);
};

void
kde_deco::handle_mode(void *)
{
	auto kde_deco = this;
	if (!kde_deco->view) {
		return;
	}

	auto client_mode = (wlr_server_decoration_manager_mode)
		kde_deco->wlr_kde_decoration->mode;

	switch (client_mode) {
	case WLR_SERVER_DECORATION_MANAGER_MODE_SERVER:
		kde_deco->view->ssd_preference = LAB_SSD_PREF_SERVER;
		break;
	case WLR_SERVER_DECORATION_MANAGER_MODE_NONE:
	case WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT:
		kde_deco->view->ssd_preference = LAB_SSD_PREF_CLIENT;
		break;
	default:
		wlr_log(WLR_ERROR, "Unspecified kde decoration variant "
			"requested: %u", client_mode);
	}

	if (kde_deco->view->ssd_preference == LAB_SSD_PREF_SERVER) {
		view_set_ssd_mode(kde_deco->view, LAB_SSD_MODE_FULL);
	} else {
		view_set_ssd_mode(kde_deco->view, LAB_SSD_MODE_NONE);
	}
}

static void
handle_new_server_decoration(struct wl_listener *listener, void *data)
{
	auto wlr_deco = (wlr_server_decoration *)data;
	auto kde_deco = new struct kde_deco();
	kde_deco->wlr_kde_decoration = wlr_deco;

	if (wlr_deco->surface) {
		/*
		 * Depending on the application event flow, the supplied
		 * wlr_surface may already have been set up as a xdg_surface
		 * or not (e.g. for GTK4). In the second case, the xdg.c
		 * new_surface handler will try to set the view via
		 * kde_server_decoration_set_view().
		 */
		struct wlr_xdg_surface *xdg_surface =
			wlr_xdg_surface_try_from_wlr_surface(wlr_deco->surface);
		if (xdg_surface && xdg_surface->data) {
			kde_deco->view = (struct view *)xdg_surface->data;
			kde_deco->handle_mode();
		}
	}

	CONNECT_LISTENER(wlr_deco, kde_deco, destroy);
	CONNECT_LISTENER(wlr_deco, kde_deco, mode);

	decorations.append(kde_deco);
}

void
kde_server_decoration_set_view(struct view *view, struct wlr_surface *surface)
{
	for (auto &kde_deco : decorations) {
		if (kde_deco.wlr_kde_decoration->surface == surface) {
			if (!kde_deco.view) {
				kde_deco.view = view;
				kde_deco.handle_mode();
			}
			return;
		}
	}
}

void
kde_server_decoration_update_default(void)
{
	assert(kde_deco_mgr);
	wlr_server_decoration_manager_set_default_mode(kde_deco_mgr,
		rc.xdg_shell_server_side_deco
		? WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
		: WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT);
}

void
kde_server_decoration_init(void)
{
	assert(!kde_deco_mgr);
	kde_deco_mgr =
		wlr_server_decoration_manager_create(g_server.wl_display);
	if (!kde_deco_mgr) {
		wlr_log(WLR_ERROR, "unable to create the kde server deco manager");
		exit(EXIT_FAILURE);
	}

	kde_server_decoration_update_default();

	wl_signal_add(&kde_deco_mgr->events.new_decoration,
		&g_server.kde_server_decoration);
	g_server.kde_server_decoration.notify = handle_new_server_decoration;
}

void
kde_server_decoration_finish(void)
{
	wl_list_remove(&g_server.kde_server_decoration.link);
}
