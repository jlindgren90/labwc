// SPDX-License-Identifier: GPL-2.0-only
#include "foreign-toplevel/wlr-foreign.h"
#include <assert.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include "labwc.h"
#include "output.h"
#include "view.h"

/* wlr signals */
void
wlr_foreign_toplevel::handle_request_minimize(void *data)
{
	auto event = (wlr_foreign_toplevel_handle_v1_minimized_event *)data;
	view_minimize(m_view, event->minimized);
}

void
wlr_foreign_toplevel::handle_request_maximize(void *data)
{
	auto event = (wlr_foreign_toplevel_handle_v1_maximized_event *)data;
	view_maximize(m_view,
		event->maximized ? VIEW_AXIS_BOTH : VIEW_AXIS_NONE,
		/*store_natural_geometry*/ true);
}

void
wlr_foreign_toplevel::handle_request_fullscreen(void *data)
{
	auto event = (wlr_foreign_toplevel_handle_v1_fullscreen_event *)data;
	/* TODO: This ignores event->output */
	view_set_fullscreen(m_view, event->fullscreen);
}

void
wlr_foreign_toplevel::handle_request_activate(void *)
{
	/* In a multi-seat world we would select seat based on event->seat here. */
	desktop_focus_view(m_view, /*raise*/ true);
}

void
wlr_foreign_toplevel::handle_request_close(void *)
{
	view_close(m_view);
}

/* Compositor signals */
void
wlr_foreign_toplevel::handle_new_app_id(void *)
{
	wlr_foreign_toplevel_handle_v1_set_app_id(m_handle, m_view->app_id.c());
}

void
wlr_foreign_toplevel::handle_new_title(void *)
{
	wlr_foreign_toplevel_handle_v1_set_title(m_handle, m_view->title.c());
}

void
wlr_foreign_toplevel::handle_new_outputs(void *)
{
	/*
	 * Loop over all outputs and notify foreign_toplevel clients about changes.
	 * wlr_foreign_toplevel_handle_v1_output_xxx() keeps track of the active
	 * outputs internally and merges the events. It also listens to output
	 * destroy events so its fine to just relay the current state and let
	 * wlr_foreign_toplevel handle the rest.
	 */
	for (auto &output : g_server.outputs) {
		if (view_on_output(m_view, &output)) {
			wlr_foreign_toplevel_handle_v1_output_enter(m_handle,
				output.wlr_output);
		} else {
			wlr_foreign_toplevel_handle_v1_output_leave(m_handle,
				output.wlr_output);
		}
	}
}

void
wlr_foreign_toplevel::handle_maximized(void *)
{
	wlr_foreign_toplevel_handle_v1_set_maximized(m_handle,
		m_view->maximized == VIEW_AXIS_BOTH);
}

void
wlr_foreign_toplevel::handle_minimized(void *)
{
	wlr_foreign_toplevel_handle_v1_set_minimized(m_handle,
		m_view->minimized);
}

void
wlr_foreign_toplevel::handle_fullscreened(void *)
{
	wlr_foreign_toplevel_handle_v1_set_fullscreen(m_handle,
		m_view->fullscreen);
}

void
wlr_foreign_toplevel::handle_activated(void *data)
{
	wlr_foreign_toplevel_handle_v1_set_activated(m_handle, *(bool *)data);
}

wlr_foreign_toplevel *
wlr_foreign_toplevel::create(view *view)
{
	assert(g_server.foreign_toplevel_manager);

	auto handle = wlr_foreign_toplevel_handle_v1_create(
		g_server.foreign_toplevel_manager);
	if (!handle) {
		wlr_log(WLR_ERROR, "cannot create wlr foreign toplevel handle for (%s)",
			view->title.c());
		return nullptr;
	}

	return new wlr_foreign_toplevel(view, handle);
}

wlr_foreign_toplevel::wlr_foreign_toplevel(struct view *view,
		wlr_foreign_toplevel_handle_v1 *handle)
	: m_view(view), m_handle(handle)
{
	/* These states may be set before the initial map */
	handle_new_app_id();
	handle_new_title();
	handle_new_outputs();
	handle_maximized();
	handle_minimized();
	handle_fullscreened();
	bool activated = view == g_server.active_view;
	handle_activated(&activated);

	/* Client side requests */
	CONNECT_LISTENER(handle, this, request_maximize);
	CONNECT_LISTENER(handle, this, request_minimize);
	CONNECT_LISTENER(handle, this, request_fullscreen);
	CONNECT_LISTENER(handle, this, request_activate);
	CONNECT_LISTENER(handle, this, request_close);
	CONNECT_LISTENER(handle, this, destroy);

	/* Compositor side state changes */
	CONNECT_LISTENER(view, this, new_app_id);
	CONNECT_LISTENER(view, this, new_title);
	CONNECT_LISTENER(view, this, new_outputs);
	CONNECT_LISTENER(view, this, maximized);
	CONNECT_LISTENER(view, this, minimized);
	CONNECT_LISTENER(view, this, fullscreened);
	CONNECT_LISTENER(view, this, activated);
}

void
wlr_foreign_toplevel::set_parent(wlr_foreign_toplevel *parent)
{
	/* The wlroots wlr-foreign-toplevel impl ensures parent is reset to NULL on destroy */
	wlr_foreign_toplevel_handle_v1_set_parent(m_handle,
		parent ? parent->m_handle : nullptr);
}

void
wlr_foreign_toplevel::destroy()
{
	/* invokes handle_destroy() which deletes "this" */
	wlr_foreign_toplevel_handle_v1_destroy(m_handle);
}
