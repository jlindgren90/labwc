// SPDX-License-Identifier: GPL-2.0-only
// Based on wlroots implementation
#include "foreign-toplevel.h"
#include <assert.h>
#include "common/mem.h"
#include "labwc.h"
#include "view.h"
#include "wlr-foreign-toplevel-management-unstable-v1-protocol.h"

#define FOREIGN_TOPLEVEL_MANAGEMENT_V1_VERSION 3

static const struct zwlr_foreign_toplevel_handle_v1_interface
	toplevel_handle_impl;

static struct view *
view_try_from_handle(struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
		&zwlr_foreign_toplevel_handle_v1_interface,
		&toplevel_handle_impl));
	return wl_resource_get_user_data(resource);
}

static void
toplevel_handle_set_maximized(struct wl_client *client,
		struct wl_resource *resource)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_maximize(view, VIEW_AXIS_BOTH);
	}
}

static void
toplevel_handle_unset_maximized(struct wl_client *client,
		struct wl_resource *resource)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_maximize(view, VIEW_AXIS_NONE);
	}
}

static void
toplevel_handle_set_minimized(struct wl_client *client,
		struct wl_resource *resource)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_minimize(view, true);
	}
}

static void
toplevel_handle_unset_minimized(struct wl_client *client,
		struct wl_resource *resource)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_minimize(view, true);
	}
}

static void
toplevel_handle_set_fullscreen(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_set_fullscreen(view, true);
	}
}

static void
toplevel_handle_unset_fullscreen(struct wl_client *client,
		struct wl_resource *resource)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_set_fullscreen(view, false);
	}
}

static void
toplevel_handle_activate(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *seat)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		desktop_focus_view(view, /*raise*/ true);
	}
}

static void
toplevel_handle_close(struct wl_client *client, struct wl_resource *resource)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_close(view);
	}
}

static void
toplevel_handle_set_rectangle(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *surface,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	/* no-op */
}

static void
toplevel_handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwlr_foreign_toplevel_handle_v1_interface
		toplevel_handle_impl = {
	.set_maximized = toplevel_handle_set_maximized,
	.unset_maximized = toplevel_handle_unset_maximized,
	.set_minimized = toplevel_handle_set_minimized,
	.unset_minimized = toplevel_handle_unset_minimized,
	.activate = toplevel_handle_activate,
	.close = toplevel_handle_close,
	.set_rectangle = toplevel_handle_set_rectangle,
	.destroy = toplevel_handle_destroy,
	.set_fullscreen = toplevel_handle_set_fullscreen,
	.unset_fullscreen = toplevel_handle_unset_fullscreen,
};

static void
toplevel_handle_send_state(struct wl_resource *resource, struct view *view)
{
	uint32_t states[4];
	size_t nstates = 0;

	if (view->st->maximized == VIEW_AXIS_BOTH) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	}
	if (view->st->minimized) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	}
	if (view->st->activated) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	}
	if (view->st->fullscreen && wl_resource_get_version(resource)
			>= ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN_SINCE_VERSION) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
	}

	struct wl_array state_array = {
		.data = states,
		.size = nstates * sizeof(states[0])
	};
	zwlr_foreign_toplevel_handle_v1_send_state(resource, &state_array);
}

static void
toplevel_handle_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
create_toplevel_handle(struct view *view, struct wl_resource *manager_resource)
{
	struct wl_client *client = wl_resource_get_client(manager_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_foreign_toplevel_handle_v1_interface,
		wl_resource_get_version(manager_resource), 0);
	die_if_null(resource);

	wl_resource_set_implementation(resource, &toplevel_handle_impl, view,
		toplevel_handle_destroyed);

	wl_list_insert(&view->foreign_toplevel_resources,
		wl_resource_get_link(resource));
	zwlr_foreign_toplevel_manager_v1_send_toplevel(manager_resource,
		resource);

	zwlr_foreign_toplevel_handle_v1_send_title(resource, view->st->title);
	zwlr_foreign_toplevel_handle_v1_send_app_id(resource, view->st->app_id);
	toplevel_handle_send_state(resource, view);
	zwlr_foreign_toplevel_handle_v1_send_done(resource);
}

void
foreign_toplevel_enable(struct view *view)
{
	assert(view);
	assert(!view->foreign_toplevel_enabled);
	assert(wl_list_empty(&view->foreign_toplevel_resources));

	struct server *server = view->server;
	assert(server->foreign_toplevel_global);

	struct wl_resource *manager_resource;
	wl_resource_for_each(manager_resource,
			&server->foreign_toplevel_resources) {
		create_toplevel_handle(view, manager_resource);
	}
	view->foreign_toplevel_enabled = true;
}

void
foreign_toplevel_disable(struct view *view)
{
	if (!view->foreign_toplevel_enabled) {
		return;
	}

	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp,
			&view->foreign_toplevel_resources) {
		zwlr_foreign_toplevel_handle_v1_send_closed(resource);
		wl_resource_set_user_data(resource, NULL);
		wl_list_remove(wl_resource_get_link(resource));
		wl_list_init(wl_resource_get_link(resource));
	}

	assert(wl_list_empty(&view->foreign_toplevel_resources));
	view->foreign_toplevel_enabled = false;
}

void
foreign_toplevel_update_app_id(struct view *view)
{
	struct wl_resource *resource;
	wl_resource_for_each(resource, &view->foreign_toplevel_resources) {
		zwlr_foreign_toplevel_handle_v1_send_app_id(resource, view->st->app_id);
		zwlr_foreign_toplevel_handle_v1_send_done(resource);
	}
}

void
foreign_toplevel_update_title(struct view *view)
{
	struct wl_resource *resource;
	wl_resource_for_each(resource, &view->foreign_toplevel_resources) {
		zwlr_foreign_toplevel_handle_v1_send_title(resource, view->st->title);
		zwlr_foreign_toplevel_handle_v1_send_done(resource);
	}
}

void
foreign_toplevel_update_state(struct view *view)
{
	struct wl_resource *resource;
	wl_resource_for_each(resource, &view->foreign_toplevel_resources) {
		toplevel_handle_send_state(resource, view);
		zwlr_foreign_toplevel_handle_v1_send_done(resource);
	}
}

static const struct zwlr_foreign_toplevel_manager_v1_interface
	foreign_toplevel_manager_impl;

static void
toplevel_manager_stop(struct wl_client *client, struct wl_resource *resource)
{
	assert(wl_resource_instance_of(resource,
		&zwlr_foreign_toplevel_manager_v1_interface,
		&foreign_toplevel_manager_impl));

	zwlr_foreign_toplevel_manager_v1_send_finished(resource);
	wl_resource_destroy(resource);
}

static const struct zwlr_foreign_toplevel_manager_v1_interface
		foreign_toplevel_manager_impl = {
	.stop = toplevel_manager_stop
};

static void
toplevel_manager_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
toplevel_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct server *server = data;
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_foreign_toplevel_manager_v1_interface, version, id);
	die_if_null(resource);

	wl_resource_set_implementation(resource, &foreign_toplevel_manager_impl,
		NULL, toplevel_manager_destroyed);

	wl_list_insert(&server->foreign_toplevel_resources,
		wl_resource_get_link(resource));

	struct view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (view->foreign_toplevel_enabled) {
			create_toplevel_handle(view, resource);
		}
	}
}

void
foreign_toplevel_manager_init(struct server *server)
{
	assert(!server->foreign_toplevel_global);

	server->foreign_toplevel_global = wl_global_create(server->wl_display,
		&zwlr_foreign_toplevel_manager_v1_interface,
		FOREIGN_TOPLEVEL_MANAGEMENT_V1_VERSION, server,
		toplevel_manager_bind);
	die_if_null(server->foreign_toplevel_global);

	wl_list_init(&server->foreign_toplevel_resources);
}

void
foreign_toplevel_manager_finish(struct server *server)
{
	if (server->foreign_toplevel_global) {
		wl_global_destroy(server->foreign_toplevel_global);
		server->foreign_toplevel_global = NULL;
	}
}
