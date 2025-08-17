// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/xwayland.h>
#include "labwc.h"
#include "xwayland.h"

void
xwayland_unmanaged::handle_grab_focus(void *)
{
	this->ever_grabbed_focus = true;
	if (this->node) {
		assert(xwayland_surface->surface);
		seat_focus_surface(xwayland_surface->surface);
	}
}

void
xwayland_unmanaged::handle_request_configure(void *data)
{
	auto ev = (wlr_xwayland_surface_configure_event *)data;

	wlr_xwayland_surface_configure(xwayland_surface, ev->x, ev->y,
		ev->width, ev->height);
	if (this->node) {
		wlr_scene_node_set_position(this->node, ev->x, ev->y);
		cursor_update_focus();
	}
}

void
xwayland_unmanaged::handle_set_geometry(void *)
{
	if (this->node) {
		wlr_scene_node_set_position(this->node, xwayland_surface->x,
			xwayland_surface->y);
		cursor_update_focus();
	}
}

void
xwayland_unmanaged::handle_map(void *)
{
	auto unmanaged = this;
	auto xsurface = unmanaged->xwayland_surface;
	assert(!unmanaged->node);

	/* Stack new surface on top */
	g_unmanaged_surfaces.append(unmanaged);

	CONNECT_LISTENER(xsurface, unmanaged, set_geometry);

	if (wlr_xwayland_surface_override_redirect_wants_focus(xsurface)
			|| unmanaged->ever_grabbed_focus) {
		seat_focus_surface(xsurface->surface);
	}

	/* node will be destroyed automatically once surface is destroyed */
	unmanaged->node = &wlr_scene_surface_create(g_server.unmanaged_tree,
		xsurface->surface)->buffer->node;
	wlr_scene_node_set_position(unmanaged->node, xsurface->x, xsurface->y);
	cursor_update_focus();
}

static void
focus_next_surface(struct wlr_xwayland_surface *xsurface)
{
	/* Try to focus on last created unmanaged xwayland surface */
	for (auto &u : g_unmanaged_surfaces.reversed()) {
		struct wlr_xwayland_surface *prev = u.xwayland_surface;
		if (wlr_xwayland_surface_override_redirect_wants_focus(prev)
				|| u.ever_grabbed_focus) {
			seat_focus_surface(prev->surface);
			return;
		}
	}

	/*
	 * Unmanaged surfaces do not clear the active view when mapped.
	 * Therefore, we can simply give the focus back to the active
	 * view when the last unmanaged surface is unmapped.
	 *
	 * Also note that resetting the focus here is only on the
	 * compositor side. On the xwayland server side, focus is never
	 * given to unmanaged surfaces to begin with - keyboard grabs
	 * are used instead.
	 *
	 * In the case of Globally Active input windows, calling
	 * view_offer_focus() at this point is both unnecessary and
	 * insufficient, since it doesn't update the seat focus
	 * immediately and ultimately results in a loss of focus.
	 *
	 * For the above reasons, we avoid calling desktop_focus_view()
	 * here and instead call seat_focus_surface() directly.
	 *
	 * If modifying this logic, please test for regressions with
	 * menus/tooltips in JetBrains CLion or similar.
	 */
	if (g_server.active_view) {
		seat_focus_surface(g_server.active_view->surface);
	}
}

void
xwayland_unmanaged::handle_unmap(void *)
{
	auto unmanaged = this;
	auto xsurface = unmanaged->xwayland_surface;
	assert(unmanaged->node);

	g_unmanaged_surfaces.remove(this);
	unmanaged->on_set_geometry.disconnect();
	wlr_scene_node_set_enabled(unmanaged->node, false);

	/*
	 * Mark the node as gone so a racing configure event
	 * won't try to reposition the node while unmapped.
	 */
	unmanaged->node = NULL;
	cursor_update_focus();

	if (g_seat.seat->keyboard_state.focused_surface == xsurface->surface) {
		focus_next_surface(xsurface);
	}
}

void
xwayland_unmanaged::handle_associate(void *)
{
	assert(xwayland_surface->surface);

	CONNECT_LISTENER(xwayland_surface->surface, this, map);
	CONNECT_LISTENER(xwayland_surface->surface, this, unmap);
}

void
xwayland_unmanaged::handle_dissociate(void *)
{
	on_map.disconnect();
	on_unmap.disconnect();
}

void
xwayland_unmanaged::handle_set_override_redirect(void *)
{
	auto xsurface = this->xwayland_surface;
	bool mapped = xsurface->surface && xsurface->surface->mapped;
	if (mapped) {
		handle_unmap(NULL);
	}
	delete this;
	/* "this" is invalid after this point */
	xwayland_view_create(xsurface, mapped);
}

void
xwayland_unmanaged::handle_request_activate(void *)
{
	if (!xwayland_surface->surface || !xwayland_surface->surface->mapped) {
		return;
	}

	/*
	 * Validate that the unmanaged surface trying to grab focus is actually
	 * a child of the active view before granting the request.
	 *
	 * FIXME: this logic is a bit incomplete/inconsistent. Refer to
	 * https://github.com/labwc/labwc/discussions/2821 for more info.
	 */
	struct view *view = g_server.active_view;
	if (view && view->type == LAB_XWAYLAND_VIEW) {
		struct wlr_xwayland_surface *surf =
			wlr_xwayland_surface_try_from_wlr_surface(view->surface);
		if (surf && surf->pid != xwayland_surface->pid) {
			return;
		}
	}

	seat_focus_surface(xwayland_surface->surface);
}

void
xwayland_unmanaged_create(struct wlr_xwayland_surface *xsurface, bool mapped)
{
	auto unmanaged = new xwayland_unmanaged(xsurface);
	/*
	 * xsurface->data is presumed to be a (struct view *) if set,
	 * so it must be left NULL for an unmanaged surface (it should
	 * be NULL already at this point).
	 */
	assert(!xsurface->data);

	CONNECT_LISTENER(xsurface, unmanaged, associate);
	CONNECT_LISTENER(xsurface, unmanaged, dissociate);
	CONNECT_LISTENER(xsurface, unmanaged, destroy);
	CONNECT_LISTENER(xsurface, unmanaged, grab_focus);
	CONNECT_LISTENER(xsurface, unmanaged, request_activate);
	CONNECT_LISTENER(xsurface, unmanaged, request_configure);
	CONNECT_LISTENER(xsurface, unmanaged, set_override_redirect);

	if (xsurface->surface) {
		unmanaged->handle_associate(NULL);
	}
	if (mapped) {
		unmanaged->handle_map(NULL);
	}
}
