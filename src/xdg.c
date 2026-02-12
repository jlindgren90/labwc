// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_dialog_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "common/macros.h"
#include "common/mem.h"
#include "decorations.h"
#include "labwc.h"
#include "menu/menu.h"
#include "node.h"
#include "output.h"
#include "util.h"
#include "view.h"
#include "view-impl-common.h"

#define LAB_XDG_SHELL_VERSION 6
#define CONFIGURE_TIMEOUT_MS 100

struct wlr_xdg_surface *
xdg_surface_from_view(struct view *view)
{
	assert(view->xdg_surface);
	return view->xdg_surface;
}

static struct wlr_xdg_toplevel *
xdg_toplevel_from_view(struct view *view)
{
	struct wlr_xdg_surface *xdg_surface = xdg_surface_from_view(view);
	assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);
	assert(xdg_surface->toplevel);
	return xdg_surface->toplevel;
}

static struct view_size_hints
xdg_toplevel_view_get_size_hints(struct view *view)
{
	assert(view);

	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);
	struct wlr_xdg_toplevel_state *state = &toplevel->current;

	return (struct view_size_hints){
		.min_width = state->min_width,
		.min_height = state->min_height,
	};
}

static void
handle_new_popup(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	xdg_popup_create(view, wlr_popup);
}

static void
set_fullscreen_from_request(struct view *view,
		struct wlr_xdg_toplevel_requested *requested)
{
	if (!view->st->fullscreen && requested->fullscreen
			&& requested->fullscreen_output) {
		view_set_output(view->id,
			output_from_wlr_output(requested->fullscreen_output));
	}
	view_fullscreen(view->id, requested->fullscreen);
}

static void
do_late_positioning(struct view *view, int width, int height)
{
	if (!view_is_floating(view->st)) {
		return;
	}
	struct wlr_box geom = {
		.x = view->st->pending.x,
		.y = view->st->pending.y,
		.width = width,
		.height = height
	};
	if (g_server.input_mode == LAB_INPUT_STATE_MOVE
			&& view == g_server.grabbed_view) {
		/* Reposition the view while anchoring it to cursor */
		interactive_anchor_to_cursor(&geom);
	} else {
		view_compute_default_geom(view->id, &geom);
		/* Ignore size adjustments (keep client size) */
		geom.width = width;
		geom.height = height;
	}
	view_set_pending_geom(view->id, geom);
}

static void
disable_fullscreen_bg(struct view *view)
{
	if (view->fullscreen_bg) {
		wlr_scene_node_set_enabled(&view->fullscreen_bg->node, false);
	}
}

/*
 * Centers any fullscreen view smaller than the full output size.
 * This should be called immediately before view_moved().
 */
static void
center_fullscreen_if_needed(struct view *view)
{
	if (!view->st->fullscreen || !output_is_usable(view->st->output)) {
		disable_fullscreen_bg(view);
		return;
	}

	struct wlr_box output_box = {0};
	wlr_output_layout_get_box(g_server.output_layout,
		view->st->output->wlr_output, &output_box);
	struct wlr_box geom = rect_center(view->st->current.width,
		view->st->current.height, output_box);
	rect_move_within(&geom, output_box);
	view_set_current_pos(view->id, geom.x, geom.y);

	if (geom.width >= output_box.width && geom.width >= output_box.height) {
		disable_fullscreen_bg(view);
		return;
	}

	if (!view->fullscreen_bg) {
		const float black[4] = {0, 0, 0, 1};
		view->fullscreen_bg =
			wlr_scene_rect_create(view->scene_tree, 0, 0, black);
		wlr_scene_node_lower_to_bottom(&view->fullscreen_bg->node);
	}

	wlr_scene_node_set_position(&view->fullscreen_bg->node,
		output_box.x - geom.x, output_box.y - geom.y);
	wlr_scene_rect_set_size(view->fullscreen_bg,
		output_box.width, output_box.height);
	wlr_scene_node_set_enabled(&view->fullscreen_bg->node, true);
}

/* TODO: reorder so this forward declaration isn't needed */
static void set_pending_configure_serial(struct view *view, uint32_t serial);

static void
handle_commit(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, commit);
	struct wlr_xdg_surface *xdg_surface = xdg_surface_from_view(view);
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);
	assert(view->surface);

	if (xdg_surface->initial_commit) {
		uint32_t serial =
			wlr_xdg_surface_schedule_configure(xdg_surface);
		if (serial > 0) {
			set_pending_configure_serial(view, serial);
		}

		uint32_t wm_caps = WLR_XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU
			| WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE
			| WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN
			| WLR_XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
		wlr_xdg_toplevel_set_wm_capabilities(toplevel, wm_caps);

		if (view->st->output) {
			wlr_xdg_toplevel_set_bounds(toplevel,
				view->st->output->usable_area.width,
				view->st->output->usable_area.height);
		}

		/*
		 * Handle initial fullscreen/maximize requests immediately after
		 * scheduling the initial configure event (before it is sent) in
		 * order to send the correct size and avoid flicker.
		 *
		 * In normal (non-fullscreen/maximized) cases, the initial
		 * configure event is sent with a zero size, which requests the
		 * application to choose its own size.
		 */
		if (toplevel->requested.fullscreen) {
			set_fullscreen_from_request(view, &toplevel->requested);
		}
		if (toplevel->requested.maximized) {
			view_maximize(view->id, VIEW_AXIS_BOTH);
		}
		return;
	}

	struct wlr_box size = xdg_surface->geometry;
	bool update_required = false;

	/*
	 * If we didn't know the natural size when leaving fullscreen or
	 * unmaximizing, then the pending size will be 0x0. In this case,
	 * the pending x/y is also unset and we still need to position
	 * the window.
	 */
	if (wlr_box_empty(&view->st->pending) && !wlr_box_empty(&size)) {
		do_late_positioning(view, size.width, size.height);
		update_required = true;
	}

	/*
	 * Qt applications occasionally fail to call set_window_geometry
	 * after a configure request, but do correctly update the actual
	 * surface extent. This results in a mismatch between the window
	 * decorations (which follow the logical geometry) and the visual
	 * size of the client area. As a workaround, we try to detect
	 * this case and ignore the out-of-date window geometry.
	 */
	if (size.width != view->st->pending.width
			|| size.height != view->st->pending.height) {
		/*
		 * Not using wlr_surface_get_extend() since Thunderbird
		 * sometimes resizes the window geometry and the toplevel
		 * surface size, but not the subsurface size (see #2183).
		 */
		struct wlr_box extent = {
			.width = view->surface->current.width,
			.height = view->surface->current.height,
		};
		if (extent.width == view->st->pending.width
				&& extent.height == view->st->pending.height) {
			wlr_log(WLR_DEBUG,
				"window geometry for client (%s) appears to be "
				"incorrect - ignoring",
				view->st->app_id);
			size = extent; /* Use surface extent instead */
		}
	}

	if (view->st->current.width != size.width
			|| view->st->current.height != size.height) {
		update_required = true;
	}

	uint32_t serial = view->pending_configure_serial;
	if (serial > 0 && serial == xdg_surface->current.configure_serial) {
		assert(view->pending_configure_timeout);
		wl_event_source_remove(view->pending_configure_timeout);
		view->pending_configure_serial = 0;
		view->pending_configure_timeout = NULL;
		update_required = true;
	}

	if (update_required) {
		view_impl_apply_geometry(view, size.width, size.height);
		center_fullscreen_if_needed(view);
		view_moved(view);

		/*
		 * Some views (e.g., terminals that scale as multiples of rows
		 * and columns, or windows that impose a fixed aspect ratio),
		 * may respond to a resize but alter the width or height. When
		 * this happens, view->pending will be out of sync with the
		 * actual geometry (size *and* position, depending on the edge
		 * from which the resize was attempted). When no other
		 * configure is pending, re-sync the pending geometry with the
		 * actual view.
		 */
		if (!view->pending_configure_serial) {
			view_set_pending_geom(view->id, view->st->current);

			/*
			 * wlroots retains the size set by any call to
			 * wlr_xdg_toplevel_set_size and will send the retained
			 * values with every subsequent configure request. If a
			 * client has resized itself in the meantime, a
			 * configure request that sends the now-outdated size
			 * may prompt the client to resize itself unexpectedly.
			 *
			 * Calling wlr_xdg_toplevel_set_size to update the
			 * value held by wlroots is undesirable here, because
			 * that will trigger another configure event and we
			 * don't want to get stuck in a request-response loop.
			 * Instead, just manipulate the dimensions that *would*
			 * be adjusted by the call, so the right values will
			 * apply next time.
			 *
			 * This is not ideal, but it is the cleanest option.
			 */
			toplevel->scheduled.width = view->st->current.width;
			toplevel->scheduled.height = view->st->current.height;
		}
	}
}

static int
handle_configure_timeout(void *data)
{
	struct view *view = data;
	assert(view->pending_configure_serial > 0);
	assert(view->pending_configure_timeout);

	wlr_log(WLR_INFO,
		"client (%s) did not respond to configure request in %d ms",
		view->st->app_id, CONFIGURE_TIMEOUT_MS);

	wl_event_source_remove(view->pending_configure_timeout);
	view->pending_configure_serial = 0;
	view->pending_configure_timeout = NULL;

	/*
	 * No need to do anything else if the view is just being slow to
	 * map - the map handler will take care of the positioning.
	 */
	if (!view->st->mapped) {
		return 0; /* ignored per wl_event_loop docs */
	}

	/*
	 * The client is taking too long to respond to a pending resize.
	 * Reset pending size to current and apply any pending move now
	 * so that the desktop doesn't appear unresponsive.
	 *
	 * Corner case: we may get here with an empty pending geometry
	 * in case of an initially-maximized view which is taking a long
	 * time to un-maximize (seen for example with Thunderbird on
	 * slow machines). In that case we have no great options (we
	 * can't center the view since we don't know the un-maximized
	 * size yet), so set a fallback position.
	 */
	struct wlr_box geom = {
		.x = view->st->pending.x,
		.y = view->st->pending.y,
		.width = view->st->current.width,
		.height = view->st->current.height
	};

	if (wlr_box_empty(&view->st->pending)) {
		wlr_log(WLR_INFO, "using fallback position");
		geom.x = VIEW_FALLBACK_X;
		geom.y = VIEW_FALLBACK_Y;
		/* At least try to keep it on the same output */
		if (output_is_usable(view->st->output)) {
			struct wlr_box box =
				output_usable_area_in_layout_coords(
					view->st->output);
			geom.x += box.x;
			geom.y += box.y;
		}
	}

	view_set_pending_geom(view->id, geom);
	view_set_current_pos(view->id, geom.x, geom.y);

	center_fullscreen_if_needed(view);
	view_moved(view);

	return 0; /* ignored per wl_event_loop docs */
}

static void
set_pending_configure_serial(struct view *view, uint32_t serial)
{
	view->pending_configure_serial = serial;
	if (!view->pending_configure_timeout) {
		view->pending_configure_timeout =
			wl_event_loop_add_timer(g_server.wl_event_loop,
				handle_configure_timeout, view);
	}
	wl_event_source_timer_update(view->pending_configure_timeout,
		CONFIGURE_TIMEOUT_MS);
}

static void
handle_destroy(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, destroy);
	assert(view->xdg_surface);

	struct wlr_xdg_popup *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &view->xdg_surface->popups, link) {
		wlr_xdg_popup_destroy(popup);
	}

	view->xdg_surface->data = NULL;
	view->xdg_surface = NULL;

	/* Remove xdg-shell view specific listeners */
	wl_list_remove(&view->set_app_id.link);
	wl_list_remove(&view->request_show_window_menu.link);
	wl_list_remove(&view->new_popup.link);
	wl_list_remove(&view->commit.link);

	if (view->pending_configure_timeout) {
		wl_event_source_remove(view->pending_configure_timeout);
		view->pending_configure_timeout = NULL;
	}

	view_destroy(view);
}

static void
handle_request_move(struct wl_listener *listener, void *data)
{
	/*
	 * This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check
	 * the provided serial against a list of button press serials sent to
	 * this client, to prevent the client from requesting this whenever they
	 * want.
	 *
	 * Note: interactive_begin() checks that view == server->grabbed_view.
	 */
	struct view *view = wl_container_of(listener, view, request_move);
	interactive_begin(view, LAB_INPUT_STATE_MOVE, LAB_EDGE_NONE);
}

static void
handle_request_resize(struct wl_listener *listener, void *data)
{
	/*
	 * This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check
	 * the provided serial against a list of button press serials sent to
	 * this client, to prevent the client from requesting this whenever they
	 * want.
	 *
	 * Note: interactive_begin() checks that view == server->grabbed_view.
	 */
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct view *view = wl_container_of(listener, view, request_resize);
	interactive_begin(view, LAB_INPUT_STATE_RESIZE, event->edges);
}

static void
handle_request_minimize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_minimize);
	view_minimize(view->id, xdg_toplevel_from_view(view)->requested.minimized);
}

static void
handle_request_maximize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_maximize);
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);

	if (!toplevel->base->initialized) {
		/*
		 * Do nothing if we have not received the initial commit yet.
		 * We will maximize the view in the commit handler.
		 */
		return;
	}

	if (!view->st->mapped && !view->st->output) {
		view_set_output(view->id, output_nearest_to_cursor());
	}
	bool maximized = toplevel->requested.maximized;
	view_maximize(view->id, maximized ? VIEW_AXIS_BOTH : VIEW_AXIS_NONE);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_fullscreen);
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);

	if (!toplevel->base->initialized) {
		/*
		 * Do nothing if we have not received the initial commit yet.
		 * We will fullscreen the view in the commit handler.
		 */
		return;
	}

	if (!view->st->mapped && !view->st->output) {
		view_set_output(view->id, output_nearest_to_cursor());
	}
	set_fullscreen_from_request(view,
		&xdg_toplevel_from_view(view)->requested);
}

static void
handle_request_show_window_menu(struct wl_listener *listener, void *data)
{
	struct view *view =
		wl_container_of(listener, view, request_show_window_menu);

	struct menu *menu = menu_get_by_id("client-menu");
	assert(menu);
	menu->triggered_by_view = view;

	struct wlr_cursor *cursor = g_seat.cursor;
	menu_open_root(menu, cursor->x, cursor->y);
}

static void
handle_set_title(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_title);
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);

	view_set_title(view->id, toplevel->title);
}

static void
handle_set_app_id(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_app_id);
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);

	view_set_app_id(view->id, toplevel->app_id);
}

void
xdg_toplevel_view_configure(struct view *view, struct wlr_box geo,
		struct wlr_box *pending, struct wlr_box *current)
{
	uint32_t serial = 0;

	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);

	/*
	 * We do not need to send a configure request unless the size
	 * changed (wayland has no notion of a global position). If the
	 * size is the same (and there is no pending configure request)
	 * then we can just move the view directly.
	 */
	if (geo.width != pending->width || geo.height != pending->height) {
		if (toplevel->base->initialized) {
			serial = wlr_xdg_toplevel_set_size(toplevel, geo.width, geo.height);
		} else {
			/*
			 * This may happen, for example, when a panel resizes because a
			 * foreign-toplevel has been destroyed. This would then trigger
			 * a call to desktop_arrange_all_views() which in turn explicitly
			 * also tries to configure unmapped surfaces. This is fine when
			 * trying to resize surfaces before they are mapped but it will
			 * also try to resize surfaces which have been unmapped but their
			 * associated struct view has not been destroyed yet.
			 */
			wlr_log(WLR_DEBUG, "Preventing configure of uninitialized surface");
		}
	}

	*pending = geo;
	if (serial > 0) {
		set_pending_configure_serial(view, serial);
	} else if (view->pending_configure_serial == 0) {
		current->x = geo.x;
		current->y = geo.y;
		view_moved(view);
	}
}

static void
xdg_toplevel_view_close(struct view *view)
{
	wlr_xdg_toplevel_send_close(xdg_toplevel_from_view(view));
}

void
xdg_toplevel_view_maximize(struct view *view, enum view_axis maximized)
{
	if (!xdg_toplevel_from_view(view)->base->initialized) {
		wlr_log(WLR_DEBUG, "Prevented maximize notification for a non-intialized view");
		return;
	}
	uint32_t serial = wlr_xdg_toplevel_set_maximized(
		xdg_toplevel_from_view(view), maximized == VIEW_AXIS_BOTH);
	if (serial > 0) {
		set_pending_configure_serial(view, serial);
	}
}

static struct view *
xdg_toplevel_view_get_parent(struct view *view)
{
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);
	return toplevel->parent ?
		(struct view *)toplevel->parent->base->data : NULL;
}

static struct wlr_xdg_toplevel *
top_parent_of(struct view *view)
{
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);
	while (toplevel->parent) {
		toplevel = toplevel->parent;
	}
	return toplevel;
}

/* Return the most senior parent (=root) view */
ViewId
xdg_toplevel_view_get_root_id(struct view *view)
{
	struct wlr_xdg_toplevel *root = top_parent_of(view);
	struct view *root_view = root->base->data;
	return root_view->id;
}

bool
xdg_toplevel_view_is_modal_dialog(struct view *view)
{
	struct wlr_xdg_toplevel *toplevel = xdg_toplevel_from_view(view);
	struct wlr_xdg_dialog_v1 *dialog =
		wlr_xdg_dialog_v1_try_from_wlr_xdg_toplevel(toplevel);
	if (!dialog) {
		return false;
	}
	return dialog->modal;
}

void
xdg_toplevel_view_set_active(struct view *view, bool active)
{
	if (!xdg_toplevel_from_view(view)->base->initialized) {
		wlr_log(WLR_DEBUG, "Prevented activating a non-intialized view");
		return;
	}
	uint32_t serial = wlr_xdg_toplevel_set_activated(
		xdg_toplevel_from_view(view), active);
	if (serial > 0) {
		set_pending_configure_serial(view, serial);
	}
}

void
xdg_toplevel_view_set_fullscreen(struct view *view, bool fullscreen)
{
	if (!xdg_toplevel_from_view(view)->base->initialized) {
		wlr_log(WLR_DEBUG, "Prevented fullscreening a non-intialized view");
		return;
	}
	uint32_t serial = wlr_xdg_toplevel_set_fullscreen(
		xdg_toplevel_from_view(view), fullscreen);
	if (serial > 0) {
		set_pending_configure_serial(view, serial);
	}
	/* Disable background fill immediately on leaving fullscreen */
	if (!fullscreen) {
		disable_fullscreen_bg(view);
	}
}

void
xdg_toplevel_view_notify_tiled(struct view *view)
{
	if (!xdg_toplevel_from_view(view)->base->initialized) {
		wlr_log(WLR_DEBUG, "Prevented tiling notification for a non-intialized view");
		return;
	}

	enum lab_edge edge = LAB_EDGE_NONE;

	/*
	 * Edge-snapped view are considered tiled on the snapped edge and those
	 * perpendicular to it.
	 */
	switch (view->st->tiled) {
	case LAB_EDGE_LEFT:
		edge = LAB_EDGES_EXCEPT_RIGHT;
		break;
	case LAB_EDGE_RIGHT:
		edge = LAB_EDGES_EXCEPT_LEFT;
		break;
	case LAB_EDGE_TOP:
		edge = LAB_EDGES_EXCEPT_BOTTOM;
		break;
	case LAB_EDGE_BOTTOM:
		edge = LAB_EDGES_EXCEPT_TOP;
		break;
	case LAB_EDGES_TOP_LEFT:
	case LAB_EDGES_TOP_RIGHT:
	case LAB_EDGES_BOTTOM_LEFT:
	case LAB_EDGES_BOTTOM_RIGHT:
		edge = view->st->tiled;
		break;
	/* TODO: LAB_EDGE_CENTER? */
	default:
		edge = LAB_EDGE_NONE;
	}

	uint32_t serial =
		wlr_xdg_toplevel_set_tiled(xdg_toplevel_from_view(view), edge);
	if (serial > 0) {
		set_pending_configure_serial(view, serial);
	}
}

static void
handle_map(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, mappable.map);
	if (view->st->mapped) {
		return;
	}

	/*
	 * An output should have been chosen when the surface was first
	 * created, but take one more opportunity to assign an output if not.
	 */
	if (!view->st->output) {
		view_set_output(view->id, output_nearest_to_cursor());
	}

	if (!view->st->ever_mapped) {
		/*
		 * Set initial "pending" dimensions. "Current"
		 * dimensions remain zero until handle_commit().
		 */
		if (wlr_box_empty(&view->st->pending)) {
			struct wlr_xdg_surface *xdg_surface =
				xdg_surface_from_view(view);
			view_set_pending_geom(view->id, (struct wlr_box) {
				.x = view->st->pending.x,
				.y = view->st->pending.y,
				.width = xdg_surface->geometry.width,
				.height = xdg_surface->geometry.height
			});
		}

		/*
		 * Set initial "pending" position for floating views.
		 */
		if (view_is_floating(view->st)) {
			struct view *parent =
				xdg_toplevel_view_get_parent(view);
			view_set_initial_geom(view->id,
				parent ? &parent->st->pending : NULL,
				/* keep_position */ false);
		}

		/*
		 * Set initial "current" position directly before
		 * calling view_moved() to reduce flicker
		 */
		view_set_current_pos(view->id, view->st->pending.x,
			view->st->pending.y);

		view_moved(view);
	}

	view_map_common(view->id, VIEW_FOCUS_MODE_ALWAYS);
}

static void
handle_unmap(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, mappable.unmap);
	if (view->st->mapped) {
		view_unmap_common(view->id);
	}
}

static const struct view_impl xdg_toplevel_view_impl = {
	.close = xdg_toplevel_view_close,
	.get_size_hints = xdg_toplevel_view_get_size_hints,
};

struct token_data {
	bool had_valid_surface;
	bool had_valid_seat;
	struct wl_listener destroy;
};

static void
handle_xdg_activation_token_destroy(struct wl_listener *listener, void *data)
{
	struct token_data *token_data = wl_container_of(listener, token_data, destroy);
	wl_list_remove(&token_data->destroy.link);
	free(token_data);
}

static void
handle_xdg_activation_new_token(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_token_v1 *token = data;
	struct token_data *token_data = znew(*token_data);
	token_data->had_valid_surface = !!token->surface;
	token_data->had_valid_seat = !!token->seat;
	token->data = token_data;

	token_data->destroy.notify = handle_xdg_activation_token_destroy;
	wl_signal_add(&token->events.destroy, &token_data->destroy);
}

static void
handle_xdg_activation_request(struct wl_listener *listener, void *data)
{
	const struct wlr_xdg_activation_v1_request_activate_event *event = data;
	struct token_data *token_data = event->token->data;
	assert(token_data);

	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(event->surface);
	if (!xdg_surface) {
		return;
	}
	struct view *view = xdg_surface->data;

	if (!view) {
		wlr_log(WLR_INFO, "Not activating surface - no view attached to surface");
		return;
	}

	if (!token_data->had_valid_seat) {
		wlr_log(WLR_INFO, "Denying focus request, seat wasn't supplied");
		return;
	}

	/*
	 * TODO: The verification of source surface is temporarily disabled to
	 * allow activation of some clients (e.g. thunderbird). Reland this
	 * check when we implement the configuration for activation policy or
	 * urgency hints.
	 *
	 * if (!token_data->had_valid_surface) {
	 *	wlr_log(WLR_INFO, "Denying focus request, source surface not set");
	 *	return;
	 * }
	 */

	wlr_log(WLR_DEBUG, "Activating surface");
	view_focus(view->id, /*raise*/ true);
}

/*
 * We use the following struct user_data pointers:
 *   - wlr_xdg_surface->data = view
 *     for the wlr_xdg_toplevel_decoration_v1 implementation
 *   - wlr_surface->data = scene_tree
 *     to help the popups find their parent nodes
 */
static void
handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel *xdg_toplevel = data;
	struct wlr_xdg_surface *xdg_surface = xdg_toplevel->base;

	assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL);

	struct view *view = znew(*view);

	view->impl = &xdg_toplevel_view_impl;
	view_init(view, /* is_xwayland */ false);

	view->xdg_surface = xdg_surface;

	/*
	 * Pick an output for the surface as soon as its created, so that the
	 * client can be notified about any fractional scale before it is given
	 * the chance to configure itself (and possibly pick its dimensions).
	 */
	view_set_output(view->id, output_nearest_to_cursor());
	if (view->st->output) {
		wlr_fractional_scale_v1_notify_scale(xdg_surface->surface,
			view->st->output->wlr_output->scale);
	}

	view->scene_tree = wlr_scene_tree_create(g_server.view_tree);
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);

	struct wlr_scene_tree *tree = wlr_scene_xdg_surface_create(
		view->scene_tree, xdg_surface);
	die_if_null(tree);

	view->content_tree = tree;
	node_descriptor_create(&view->scene_tree->node,
		LAB_NODE_VIEW, view, /*data*/ NULL);

	/*
	 * xdg_toplevel_decoration and kde_server_decoration use this
	 * pointer to connect the view to a decoration object that may
	 * be created in the future.
	 */
	xdg_surface->data = view;

	/*
	 * GTK4 initializes the decorations on the wl_surface before
	 * converting it into a xdg surface. This call takes care of
	 * connecting the view to an existing decoration. If there
	 * is no existing decoration object available for the
	 * wl_surface, this call is a no-op.
	 */
	kde_server_decoration_set_view(view, xdg_surface->surface);

	/* In support of xdg popups and IME popup */
	view->surface = xdg_surface->surface;
	view->surface->data = tree;

	mappable_connect(&view->mappable, xdg_surface->surface,
		handle_map, handle_unmap);

	struct wlr_xdg_toplevel *toplevel = xdg_surface->toplevel;
	CONNECT_SIGNAL(toplevel, view, destroy);
	CONNECT_SIGNAL(toplevel, view, request_move);
	CONNECT_SIGNAL(toplevel, view, request_resize);
	CONNECT_SIGNAL(toplevel, view, request_minimize);
	CONNECT_SIGNAL(toplevel, view, request_maximize);
	CONNECT_SIGNAL(toplevel, view, request_fullscreen);
	CONNECT_SIGNAL(toplevel, view, set_title);
	CONNECT_SIGNAL(view->surface, view, commit);

	/* Events specific to XDG toplevel views */
	CONNECT_SIGNAL(toplevel, view, set_app_id);
	CONNECT_SIGNAL(toplevel, view, request_show_window_menu);
	CONNECT_SIGNAL(xdg_surface, view, new_popup);
}

void
xdg_shell_init(void)
{
	g_server.xdg_shell = wlr_xdg_shell_create(g_server.wl_display,
		LAB_XDG_SHELL_VERSION);
	if (!g_server.xdg_shell) {
		wlr_log(WLR_ERROR, "unable to create the XDG shell interface");
		exit(EXIT_FAILURE);
	}

	g_server.new_xdg_toplevel.notify = handle_new_xdg_toplevel;
	wl_signal_add(&g_server.xdg_shell->events.new_toplevel,
		&g_server.new_xdg_toplevel);

	g_server.xdg_activation =
		wlr_xdg_activation_v1_create(g_server.wl_display);
	if (!g_server.xdg_activation) {
		wlr_log(WLR_ERROR, "unable to create xdg_activation interface");
		exit(EXIT_FAILURE);
	}

	g_server.xdg_activation_request.notify = handle_xdg_activation_request;
	wl_signal_add(&g_server.xdg_activation->events.request_activate,
		&g_server.xdg_activation_request);

	g_server.xdg_activation_new_token.notify =
		handle_xdg_activation_new_token;
	wl_signal_add(&g_server.xdg_activation->events.new_token,
		&g_server.xdg_activation_new_token);

	wlr_xdg_wm_dialog_v1_create(g_server.wl_display, 1);
}

void
xdg_shell_finish(void)
{
	wl_list_remove(&g_server.new_xdg_toplevel.link);
	wl_list_remove(&g_server.xdg_activation_request.link);
	wl_list_remove(&g_server.xdg_activation_new_token.link);
}
