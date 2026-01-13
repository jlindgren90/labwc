// SPDX-License-Identifier: GPL-2.0-only
//
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
		view_minimize(view, false);
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
	// no-op
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
toplevel_handle_destroyed(struct wl_resource *resource)
{
	struct view *view = view_try_from_handle(resource);
	if (view) {
		view_remove_foreign_toplevel(view->id, resource);
	}
}

struct wl_resource *
foreign_toplevel_create(struct wl_resource *client_resource, struct view *view)
{
	struct wl_client *client = wl_resource_get_client(client_resource);
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_foreign_toplevel_handle_v1_interface,
		wl_resource_get_version(client_resource), 0);
	die_if_null(resource);

	wl_resource_set_implementation(resource, &toplevel_handle_impl, view,
		toplevel_handle_destroyed);
	zwlr_foreign_toplevel_manager_v1_send_toplevel(client_resource,
		resource);

	return resource;
}

void
foreign_toplevel_send_app_id(struct wl_resource *resource, const char *app_id)
{
	zwlr_foreign_toplevel_handle_v1_send_app_id(resource, app_id);
}

void
foreign_toplevel_send_title(struct wl_resource *resource, const char *title)
{
	zwlr_foreign_toplevel_handle_v1_send_title(resource, title);
}

void
foreign_toplevel_send_state(struct wl_resource *resource, ForeignToplevelState state)
{
	uint32_t states[4];
	size_t nstates = 0;

	if (state.maximized) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
	}
	if (state.minimized) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
	}
	if (state.activated) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
	}
	if (state.fullscreen && wl_resource_get_version(resource)
			>= ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN_SINCE_VERSION) {
		states[nstates++] = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
	}

	struct wl_array state_array = {
		.data = states,
		.size = nstates * sizeof(states[0])
	};
	zwlr_foreign_toplevel_handle_v1_send_state(resource, &state_array);
}

void
foreign_toplevel_send_done(struct wl_resource *resource)
{
	zwlr_foreign_toplevel_handle_v1_send_done(resource);
}

void
foreign_toplevel_close(struct wl_resource *resource)
{
	// Send "closed" and unlink the resource from the view;
	// the handle will be destroyed by the client later
	zwlr_foreign_toplevel_handle_v1_send_closed(resource);
	wl_resource_set_user_data(resource, NULL);
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
toplevel_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client,
		&zwlr_foreign_toplevel_manager_v1_interface, version, id);
	die_if_null(resource);

	wl_resource_set_implementation(resource, &foreign_toplevel_manager_impl,
		NULL, views_remove_foreign_toplevel_client);
	views_add_foreign_toplevel_client(resource);
}

static struct wl_global *foreign_toplevel_global;

void
foreign_toplevel_manager_init(struct wl_display *display)
{
	if (!foreign_toplevel_global) {
		foreign_toplevel_global = wl_global_create(display,
			&zwlr_foreign_toplevel_manager_v1_interface,
			FOREIGN_TOPLEVEL_MANAGEMENT_V1_VERSION, NULL,
			toplevel_manager_bind);
		die_if_null(foreign_toplevel_global);
	}
}

void
foreign_toplevel_manager_finish(void)
{
	if (foreign_toplevel_global) {
		wl_global_destroy(foreign_toplevel_global);
		foreign_toplevel_global = NULL;
	}
}
