// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "common/mem.h"
#include "decorations.h"
#include "view.h"
#include "xdg-decoration-unstable-v1-protocol.h"

#define DECORATION_MANAGER_VERSION 1

static const struct zxdg_toplevel_decoration_v1_interface xdg_deco_impl;

static ViewId
view_id_from_resource(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
		&zxdg_toplevel_decoration_v1_interface, &xdg_deco_impl));
	return (ViewId)wl_resource_get_user_data(resource);
}

static void
xdg_deco_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
xdg_deco_handle_set_mode(struct wl_client *client, struct wl_resource *resource,
		enum zxdg_toplevel_decoration_v1_mode mode)
{
	view_enable_ssd(view_id_from_resource(resource),
		mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	zxdg_toplevel_decoration_v1_send_configure(resource, mode);
}

static void
xdg_deco_handle_unset_mode(struct wl_client *client,
		struct wl_resource *resource)
{
	view_enable_ssd(view_id_from_resource(resource), true);
	zxdg_toplevel_decoration_v1_send_configure(resource,
		ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_interface xdg_deco_impl = {
	.destroy = xdg_deco_handle_destroy,
	.set_mode = xdg_deco_handle_set_mode,
	.unset_mode = xdg_deco_handle_unset_mode,
};

static void
xdg_deco_manager_handle_get_toplevel_decoration(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *toplevel_resource)
{
	struct wlr_xdg_toplevel *toplevel =
		wlr_xdg_toplevel_from_resource(toplevel_resource);

	ViewId view_id = (ViewId)toplevel->base->data;
	const ViewState *view_st = view_get_state(view_id);
	if (!view_st) {
		return;
	}

	struct wl_resource *resource = wl_resource_create(
		client, &zxdg_toplevel_decoration_v1_interface,
		wl_resource_get_version(manager_resource), id);
	die_if_null(resource);
	wl_resource_set_implementation(resource, &xdg_deco_impl,
		(void *)view_id, NULL);

	view_enable_ssd(view_id, true);
	zxdg_toplevel_decoration_v1_send_configure(resource,
		ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void
xdg_deco_manager_handle_destroy(struct wl_client *client,
		struct wl_resource *manager_resource)
{
	wl_resource_destroy(manager_resource);
}

static const struct zxdg_decoration_manager_v1_interface xdg_deco_manager_impl = {
	.destroy = xdg_deco_manager_handle_destroy,
	.get_toplevel_decoration = xdg_deco_manager_handle_get_toplevel_decoration,
};

static void
xdg_deco_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client,
		&zxdg_decoration_manager_v1_interface, version, id);
	die_if_null(resource);
	wl_resource_set_implementation(resource, &xdg_deco_manager_impl, NULL, NULL);
}

static struct wl_global *xdg_deco_mgr_global;

void
xdg_deco_manager_init(struct wl_display *display)
{
	xdg_deco_mgr_global = wl_global_create(
		display, &zxdg_decoration_manager_v1_interface,
		DECORATION_MANAGER_VERSION, NULL, xdg_deco_manager_bind);
	die_if_null(xdg_deco_mgr_global);
}

void
xdg_deco_manager_finish(void)
{
	wl_global_destroy(xdg_deco_mgr_global);
}
