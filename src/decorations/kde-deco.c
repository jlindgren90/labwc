// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "common/mem.h"
#include "decorations.h"
#include "server-decoration-protocol.h"
#include "view.h"

#define SERVER_DECORATION_MANAGER_VERSION 1

static const struct org_kde_kwin_server_decoration_interface kde_deco_impl;

static ViewId
view_id_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
		&org_kde_kwin_server_decoration_interface, &kde_deco_impl));
	return (ViewId)wl_resource_get_user_data(resource);
}

static void
kde_deco_handle_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
kde_deco_handle_request_mode(struct wl_client *client,
		struct wl_resource *resource, uint32_t mode)
{
	org_kde_kwin_server_decoration_send_mode(resource, mode);
	view_enable_ssd(view_id_from_resource(resource),
		mode == ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER);
}

static const struct org_kde_kwin_server_decoration_interface kde_deco_impl = {
	.release = kde_deco_handle_release,
	.request_mode = kde_deco_handle_request_mode,
};

static void
kde_deco_manager_handle_create(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *surface_resource)
{
	struct wlr_surface *surface =
		wlr_surface_from_resource(surface_resource);
	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(surface);
	if (!xdg_surface) {
		return;
	}

	ViewId view_id = (ViewId)xdg_surface->data;

	int version = wl_resource_get_version(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&org_kde_kwin_server_decoration_interface, version, id);
	die_if_null(resource);
	wl_resource_set_implementation(resource,
		&kde_deco_impl, (void *)view_id, NULL);

	org_kde_kwin_server_decoration_send_mode(resource,
		ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER);
	view_enable_ssd(view_id, true);
}

static const struct org_kde_kwin_server_decoration_manager_interface
		kde_deco_manager_impl = {
	.create = kde_deco_manager_handle_create,
};

static void
kde_deco_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client,
		&org_kde_kwin_server_decoration_manager_interface, version, id);
	die_if_null(resource);
	wl_resource_set_implementation(resource, &kde_deco_manager_impl, NULL, NULL);

	org_kde_kwin_server_decoration_manager_send_default_mode(resource,
		ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_SERVER);
}

static struct wl_global *kde_deco_mgr_global;

void
kde_deco_manager_init(struct wl_display *display)
{
	kde_deco_mgr_global = wl_global_create(display,
		&org_kde_kwin_server_decoration_manager_interface,
		SERVER_DECORATION_MANAGER_VERSION, NULL, kde_deco_manager_bind);
	die_if_null(kde_deco_mgr_global);
}

void
kde_deco_manager_finish(void)
{
	wl_global_destroy(kde_deco_mgr_global);
}
