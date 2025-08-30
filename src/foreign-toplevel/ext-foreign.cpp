// SPDX-License-Identifier: GPL-2.0-only
#include "foreign-toplevel/ext-foreign.h"
#include <assert.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include "labwc.h"
#include "view.h"

/* Compositor signals */
void
ext_foreign_toplevel::handle_new_app_id(void *)
{
	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view_get_string_prop(m_view, "title"),
		.app_id = view_get_string_prop(m_view, "app_id")
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(m_handle, &state);
}

void
ext_foreign_toplevel::handle_new_title(void *)
{
	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view_get_string_prop(m_view, "title"),
		.app_id = view_get_string_prop(m_view, "app_id")
	};
	wlr_ext_foreign_toplevel_handle_v1_update_state(m_handle, &state);
}

ext_foreign_toplevel *
ext_foreign_toplevel::create(view *view)
{
	assert(g_server.foreign_toplevel_list);

	struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = view_get_string_prop(view, "title"),
		.app_id = view_get_string_prop(view, "app_id")
	};
	auto handle = wlr_ext_foreign_toplevel_handle_v1_create(
		g_server.foreign_toplevel_list, &state);

	if (!handle) {
		wlr_log(WLR_ERROR, "cannot create ext toplevel handle for (%s)",
			view_get_string_prop(view, "title"));
		return nullptr;
	}

	return new ext_foreign_toplevel(view, handle);
}

ext_foreign_toplevel::ext_foreign_toplevel(view *view,
		wlr_ext_foreign_toplevel_handle_v1 *handle)
	: m_view(view), m_handle(handle)
{
	/* Client side requests */
	CONNECT_LISTENER(handle, this, destroy);

	/* Compositor side state changes */
	CONNECT_LISTENER(view, this, new_app_id);
	CONNECT_LISTENER(view, this, new_title);
}

void
ext_foreign_toplevel::destroy()
{
	/* invokes handle_destroy() which deletes "this" */
	wlr_ext_foreign_toplevel_handle_v1_destroy(m_handle);
}
