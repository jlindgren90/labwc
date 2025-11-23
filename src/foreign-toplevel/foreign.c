// SPDX-License-Identifier: GPL-2.0-only
#include "foreign-toplevel/foreign.h"
#include <assert.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "common/macros.h"
#include "common/mem.h"
#include "labwc.h"
#include "output.h"
#include "view.h"

struct foreign_toplevel {
	struct view *view;
	struct wlr_ext_foreign_toplevel_handle_v1 *ext_handle;
	struct wlr_foreign_toplevel_handle_v1 *wlr_handle;

	/* wlr-foreign-toplevel events */
	struct wl_listener request_maximize;
	struct wl_listener request_minimize;
	struct wl_listener request_fullscreen;
	struct wl_listener request_activate;
	struct wl_listener request_close;
};

static void
handle_request_minimize(struct wl_listener *listener, void *data)
{
	struct foreign_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_minimize);
	struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;

	view_minimize(toplevel->view, event->minimized);
}

static void
handle_request_maximize(struct wl_listener *listener, void *data)
{
	struct foreign_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;

	view_maximize(toplevel->view,
		event->maximized ? VIEW_AXIS_BOTH : VIEW_AXIS_NONE,
		/*store_natural_geometry*/ true);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct foreign_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

	/* TODO: This ignores event->output */
	view_set_fullscreen(toplevel->view, event->fullscreen);
}

static void
handle_request_activate(struct wl_listener *listener, void *data)
{
	struct foreign_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_activate);

	/* In a multi-seat world we would select seat based on event->seat here. */
	desktop_focus_view(toplevel->view, /*raise*/ true);
}

static void
handle_request_close(struct wl_listener *listener, void *data)
{
	struct foreign_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_close);

	view_close(toplevel->view);
}

struct foreign_toplevel *
foreign_toplevel_create(struct view *view)
{
	assert(view);
	struct server *server = view->server;
	assert(server->foreign_toplevel_list);
	assert(server->foreign_toplevel_manager);

	struct foreign_toplevel *toplevel = znew(*toplevel);
	toplevel->view = view;

	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view->st->title,
		.app_id = view->st->app_id,
	};
	toplevel->ext_handle = wlr_ext_foreign_toplevel_handle_v1_create(
		server->foreign_toplevel_list, &state);
	die_if_null(toplevel->ext_handle);

	toplevel->wlr_handle = wlr_foreign_toplevel_handle_v1_create(
		view->server->foreign_toplevel_manager);
	die_if_null(toplevel->wlr_handle);

	/* These states may be set before the initial map */
	wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->wlr_handle,
		view->st->app_id);
	wlr_foreign_toplevel_handle_v1_set_title(toplevel->wlr_handle,
		view->st->title);

	foreign_toplevel_update_outputs(toplevel);
	foreign_toplevel_update_maximized(toplevel);
	foreign_toplevel_update_minimized(toplevel);
	foreign_toplevel_update_fullscreen(toplevel);
	foreign_toplevel_update_activated(toplevel);

	/* Client side requests */
	CONNECT_SIGNAL(toplevel->wlr_handle, toplevel, request_maximize);
	CONNECT_SIGNAL(toplevel->wlr_handle, toplevel, request_minimize);
	CONNECT_SIGNAL(toplevel->wlr_handle, toplevel, request_fullscreen);
	CONNECT_SIGNAL(toplevel->wlr_handle, toplevel, request_activate);
	CONNECT_SIGNAL(toplevel->wlr_handle, toplevel, request_close);

	return toplevel;
}

void
foreign_toplevel_set_parent(struct foreign_toplevel *toplevel,
		struct foreign_toplevel *parent)
{
	assert(toplevel);
	assert(toplevel->wlr_handle);

	/* The wlroots wlr-foreign-toplevel impl ensures parent is reset to NULL on destroy */
	wlr_foreign_toplevel_handle_v1_set_parent(toplevel->wlr_handle,
		parent ? parent->wlr_handle : NULL);
}

void
foreign_toplevel_update_app_id(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->ext_handle);
	assert(toplevel->wlr_handle);

	struct view *view = toplevel->view;
	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view->st->title,
		.app_id = view->st->app_id,
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(toplevel->ext_handle,
		&state);
	wlr_foreign_toplevel_handle_v1_set_app_id(
		view->foreign_toplevel->wlr_handle, view->st->app_id);
}

void
foreign_toplevel_update_title(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->ext_handle);
	assert(toplevel->wlr_handle);

	struct view *view = toplevel->view;
	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view->st->title,
		.app_id = view->st->app_id,
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(toplevel->ext_handle,
		&state);
	wlr_foreign_toplevel_handle_v1_set_title(toplevel->wlr_handle,
		view->st->title);
}

void
foreign_toplevel_update_outputs(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->wlr_handle);

	/*
	 * Loop over all outputs and notify foreign_toplevel clients about changes.
	 * wlr_foreign_toplevel_handle_v1_output_xxx() keeps track of the active
	 * outputs internally and merges the events. It also listens to output
	 * destroy events so its fine to just relay the current state and let
	 * wlr_foreign_toplevel handle the rest.
	 */
	struct output *output;
	wl_list_for_each(output, &toplevel->view->server->outputs, link) {
		if (view_on_output(toplevel->view, output)) {
			wlr_foreign_toplevel_handle_v1_output_enter(
				toplevel->wlr_handle, output->wlr_output);
		} else {
			wlr_foreign_toplevel_handle_v1_output_leave(
				toplevel->wlr_handle, output->wlr_output);
		}
	}
}

void
foreign_toplevel_update_maximized(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->wlr_handle);

	wlr_foreign_toplevel_handle_v1_set_maximized(toplevel->wlr_handle,
		toplevel->view->st->maximized == VIEW_AXIS_BOTH);
}

void
foreign_toplevel_update_minimized(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->wlr_handle);

	wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->wlr_handle,
		toplevel->view->st->minimized);
}

void
foreign_toplevel_update_fullscreen(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->wlr_handle);

	wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->wlr_handle,
		toplevel->view->st->fullscreen);
}

void
foreign_toplevel_update_activated(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->wlr_handle);

	wlr_foreign_toplevel_handle_v1_set_activated(toplevel->wlr_handle,
		toplevel->view == toplevel->view->server->active_view);
}

void
foreign_toplevel_destroy(struct foreign_toplevel *toplevel)
{
	assert(toplevel);
	assert(toplevel->ext_handle);
	assert(toplevel->wlr_handle);

	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_minimize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);
	wl_list_remove(&toplevel->request_activate.link);
	wl_list_remove(&toplevel->request_close.link);

	wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->ext_handle);
	wlr_foreign_toplevel_handle_v1_destroy(toplevel->wlr_handle);

	free(toplevel);
}
