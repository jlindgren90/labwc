// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "xwayland-view.h"
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "common/list.h"
#include "common/macros.h"
#include "common/mem.h"
#include "config/rcxml.h"
#include "config/session.h"
#include "labwc.h"
#include "output.h"
#include "view.h"
#include "xwayland/server.h"
#include "xwayland/xwayland.h"

static void handle_map(struct wl_listener *listener, void *data);
static void handle_unmap(struct wl_listener *listener, void *data);

static void xwayland_view_create(struct xwayland_surface *xsurface, bool mapped);
static void xwayland_unmanaged_create(struct xwayland_surface *xsurface, bool mapped);

static struct xwayland_surface *
xwayland_surface_from_view(struct view *view)
{
	assert(view->xwayland_surface);
	return view->xwayland_surface;
}

struct view_size_hints
xwayland_view_get_size_hints(struct view *view)
{
	xcb_size_hints_t *hints = xwayland_surface_from_view(view)->size_hints;
	if (!hints) {
		return (struct view_size_hints){0};
	}
	return (struct view_size_hints){
		.min_width = hints->min_width,
		.min_height = hints->min_height,
		.width_inc = hints->width_inc,
		.height_inc = hints->height_inc,
		.base_width = hints->base_width,
		.base_height = hints->base_height,
	};
}

static enum view_focus_mode
xwayland_view_focus_mode(struct view *view)
{
	struct xwayland_surface *xsurface =
		xwayland_surface_from_view(view);

	switch (xwayland_surface_icccm_input_model(xsurface)) {
	/*
	 * Abbreviated from ICCCM section 4.1.7 (Input Focus):
	 *
	 * Passive Input - The client expects keyboard input but never
	 * explicitly sets the input focus.
	 * Locally Active Input - The client expects keyboard input and
	 * explicitly sets the input focus, but it only does so when one
	 * of its windows already has the focus.
	 *
	 * Passive and Locally Active clients set the input field of
	 * WM_HINTS to True, which indicates that they require window
	 * manager assistance in acquiring the input focus.
	 */
	case WLR_ICCCM_INPUT_MODEL_PASSIVE:
	case WLR_ICCCM_INPUT_MODEL_LOCAL:
		return VIEW_FOCUS_MODE_ALWAYS;

	/*
	 * Globally Active Input - The client expects keyboard input and
	 * explicitly sets the input focus, even when it is in windows
	 * the client does not own. ... It wants to prevent the window
	 * manager from setting the input focus to any of its windows
	 * [because it may or may not want focus].
	 *
	 * Globally Active client windows may receive a WM_TAKE_FOCUS
	 * message from the window manager. If they want the focus, they
	 * should respond with a SetInputFocus request.
	 */
	case WLR_ICCCM_INPUT_MODEL_GLOBAL:
		/*
		 * Assume that NORMAL and DIALOG windows are likely to
		 * want focus. These window types should show up in the
		 * Alt-Tab switcher and be automatically focused when
		 * they become topmost.
		 */
		return (xwayland_surface_has_window_type(xsurface,
				XWAYLAND_NET_WM_WINDOW_TYPE_NORMAL)
			|| xwayland_surface_has_window_type(xsurface,
				XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG))
			? VIEW_FOCUS_MODE_LIKELY : VIEW_FOCUS_MODE_UNLIKELY;

	/*
	 * No Input - The client never expects keyboard input.
	 *
	 * No Input and Globally Active clients set the input field to
	 * False, which requests that the window manager not set the
	 * input focus to their top-level window.
	 */
	case WLR_ICCCM_INPUT_MODEL_NONE:
		break;
	}

	return VIEW_FOCUS_MODE_NEVER;
}

bool
xwayland_view_has_strut_partial(struct view *view)
{
	struct xwayland_surface *xsurface =
		xwayland_surface_from_view(view);
	return (bool)xsurface->strut_partial;
}

void
xwayland_view_raise(struct view *view)
{
	xwayland_surface_restack(view->xwayland_surface, NULL,
		XCB_STACK_MODE_ABOVE);
}

void
xwayland_view_offer_focus(struct view *view)
{
	xwayland_surface_offer_focus(xwayland_surface_from_view(view));
}

static struct xwayland_surface *
top_parent_of(struct view *view)
{
	struct xwayland_surface *s = xwayland_surface_from_view(view);
	while (s->parent) {
		s = s->parent;
	}
	return s;
}

static bool
has_position_hint(struct xwayland_surface *xwayland_surface)
{
	return xwayland_surface->size_hints
		&& (xwayland_surface->size_hints->flags
			& (XCB_ICCCM_SIZE_HINT_US_POSITION
			| XCB_ICCCM_SIZE_HINT_P_POSITION));
}

static bool
want_deco(struct xwayland_surface *xwayland_surface)
{
	return xwayland_surface->decorations ==
		XWAYLAND_SURFACE_DECORATIONS_ALL;
}

struct view_surface_geom
xwayland_view_get_surface_geom(struct view *view)
{
	struct xwayland_surface *xwayland_surface =
		xwayland_surface_from_view(view);

	return (struct view_surface_geom) {
		.geom.x = xwayland_surface->x,
		.geom.y = xwayland_surface->y,
		.geom.width = xwayland_surface->width,
		.geom.height = xwayland_surface->height,
		.keep_position = has_position_hint(xwayland_surface),
		.use_ssd = want_deco(xwayland_surface)
	};
}

static void
handle_commit(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, commit);

	if (!view->id) {
		/* unmanaged surface */
		return;
	}

	/* Must receive commit signal before accessing surface->current* */
	struct wlr_surface_state *state =
		&view->xwayland_surface->surface->current;

	/*
	 * If there is a pending move/resize, wait until the surface
	 * size changes to update geometry. The hope is to update both
	 * the position and the size of the view at the same time,
	 * reducing visual glitches.
	 */
	if (view->st->current.width != state->width
			|| view->st->current.height != state->height) {
		view_commit_geom(view->id, state->width, state->height);
	}
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
	 * Note: interactive_begin() checks that view == g_server.grabbed_view.
	 */
	struct view *view = wl_container_of(listener, view, request_move);
	interactive_begin(view->id, LAB_INPUT_STATE_MOVE, LAB_EDGE_NONE);
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
	 * Note: interactive_begin() checks that view == g_server.grabbed_view.
	 */
	struct xwayland_resize_event *event = data;
	struct view *view = wl_container_of(listener, view, request_resize);
	interactive_begin(view->id, LAB_INPUT_STATE_RESIZE, event->edges);
}

static void
handle_associate(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, associate);
	struct wlr_surface *surface = view->xwayland_surface->surface;

	CONNECT_SIGNAL(surface, view, commit);
	CONNECT_SIGNAL(surface, view, map);
	CONNECT_SIGNAL(surface, view, unmap);
}

static void
handle_dissociate(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, dissociate);

	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
}

static void
handle_destroy(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, destroy);
	assert(view->xwayland_surface);

	if (!view->id) {
		/* unmanaged surface */
		wl_list_remove(&view->associate.link);
		wl_list_remove(&view->dissociate.link);
		wl_list_remove(&view->grab_focus.link);
		wl_list_remove(&view->request_activate.link);
		wl_list_remove(&view->request_configure.link);
		wl_list_remove(&view->set_override_redirect.link);
		wl_list_remove(&view->destroy.link);
		free(view);
		return;
	}

	/*
	 * Break view <-> xsurface association.  Note that the xsurface
	 * may not actually be destroyed at this point; it may become an
	 * "unmanaged" surface instead (in that case it is important
	 * that xsurface->data not point to the destroyed view).
	 */
	view->xwayland_surface->data = NULL;
	view->xwayland_surface = NULL;

	/* Remove XWayland view specific listeners */
	wl_list_remove(&view->associate.link);
	wl_list_remove(&view->dissociate.link);
	wl_list_remove(&view->request_above.link);
	wl_list_remove(&view->request_activate.link);
	wl_list_remove(&view->request_configure.link);
	wl_list_remove(&view->set_class.link);
	wl_list_remove(&view->set_decorations.link);
	wl_list_remove(&view->set_override_redirect.link);
	wl_list_remove(&view->set_strut_partial.link);
	wl_list_remove(&view->set_window_type.link);
	wl_list_remove(&view->set_icon.link);
	wl_list_remove(&view->focus_in.link);
	wl_list_remove(&view->map_request.link);

	view_destroy(view);
}

void
xwayland_view_configure(struct view *view, struct wlr_box geo, bool *commit_move)
{
	xwayland_surface_configure(xwayland_surface_from_view(view),
		geo.x, geo.y, geo.width, geo.height);

	/*
	 * For unknown reasons, XWayland surfaces that are completely
	 * offscreen seem not to generate commit events. In rare cases,
	 * this can prevent an offscreen window from moving onscreen
	 * (since we wait for a commit event that never occurs). As a
	 * workaround, move offscreen surfaces immediately.
	 */
	bool is_offscreen = !wlr_box_empty(&view->st->current)
		&& !wlr_output_layout_intersects(g_server.output_layout, NULL,
			&view->st->current);

	/* If not resizing, process the move immediately */
	if (is_offscreen || (view->st->current.width == geo.width
			&& view->st->current.height == geo.height)) {
		*commit_move = true;
	}
}

static void
handle_request_configure(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_configure);
	struct xwayland_surface_configure_event *event = data;

	if (!view->id) {
		/* Unmanaged surface */
		xwayland_surface_configure(view->xwayland_surface,
			event->x, event->y, event->width, event->height);
		if (view->node) {
			wlr_scene_node_set_position(view->node, event->x, event->y);
			cursor_update_focus();
		}
	} else if (view_is_floating(view->st)) {
		/* Honor client configure requests for floating views */
		struct wlr_box box = {.x = event->x, .y = event->y,
			.width = event->width, .height = event->height};
		view_adjust_size(view->id, &box.width, &box.height);
		view_move_resize(view->id, box);
	} else {
		/*
		 * Do not allow clients to request geometry other than
		 * what we computed for maximized/fullscreen/tiled
		 * views. Ignore the client request and send back a
		 * ConfigureNotify event with the computed geometry.
		 */
		const struct wlr_box *pending = &view->st->pending;
		xwayland_surface_configure(view->xwayland_surface,
			pending->x, pending->y, pending->width, pending->height);
	}
}

static void
handle_request_above(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_above);
	view_set_always_on_top(view->id, view->xwayland_surface->above);
}

static void
handle_request_activate(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_activate);

	if (view->id) {
		view_focus(view->id, /*raise*/ true);
	} else {
		/* unmanaged surface */
		struct xwayland_surface *xsurface = view->xwayland_surface;
		if (xsurface->surface && xsurface->surface->mapped) {
			seat_focus_surface(xsurface->surface);
		}
	}
}

static void
handle_request_minimize(struct wl_listener *listener, void *data)
{
	struct xwayland_minimize_event *event = data;
	struct view *view = wl_container_of(listener, view, request_minimize);
	view_minimize(view->id, event->minimize);
}

static void
handle_request_maximize(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_maximize);
	struct xwayland_surface *surf = xwayland_surface_from_view(view);

	enum view_axis maximize = VIEW_AXIS_NONE;
	if (surf->maximized_vert) {
		maximize |= VIEW_AXIS_VERTICAL;
	}
	if (surf->maximized_horz) {
		maximize |= VIEW_AXIS_HORIZONTAL;
	}
	view_maximize(view->id, maximize);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, request_fullscreen);
	view_fullscreen(view->id, xwayland_surface_from_view(view)->fullscreen,
		/* output */ NULL);
}

static void
handle_set_title(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_title);
	view_set_title(view->id, view->xwayland_surface->title);
}

static void
handle_set_class(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_class);

	/*
	 * Use the WM_CLASS 'instance' (1st string) for the app_id. Per
	 * ICCCM, this is usually "the trailing part of the name used to
	 * invoke the program (argv[0] stripped of any directory names)".
	 *
	 * In most cases, the 'class' (2nd string) is the same as the
	 * 'instance' except for being capitalized. We want lowercase
	 * here since we use the app_id for icon lookups.
	 */
	view_set_app_id(view->id, view->xwayland_surface->instance);
}

void
xwayland_view_close(struct view *view)
{
	xwayland_surface_close(xwayland_surface_from_view(view));
}

static void
handle_set_decorations(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_decorations);
	view_enable_ssd(view->id, want_deco(view->xwayland_surface));
}

static void
handle_set_window_type(struct wl_listener *listener, void *data)
{
	/* Intentionally left blank */
}

static void
handle_set_override_redirect(struct wl_listener *listener, void *data)
{
	struct view *view =
		wl_container_of(listener, view, set_override_redirect);
	struct xwayland_surface *xsurface = view->xwayland_surface;

	bool mapped = xsurface->surface && xsurface->surface->mapped;
	if (mapped) {
		handle_unmap(&view->unmap, NULL);
	}
	if (xsurface->surface) {
		handle_dissociate(&view->dissociate, NULL);
	}
	handle_destroy(&view->destroy, xsurface);
	/* view is invalid after this point */
	if (xsurface->override_redirect) {
		xwayland_unmanaged_create(xsurface, mapped);
	} else {
		xwayland_view_create(xsurface, mapped);
	}
}

static void
handle_set_strut_partial(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_strut_partial);

	if (view->st->mapped) {
		output_update_all_usable_areas(false);
	}
}

static void
handle_set_icon(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_icon);

	view_clear_icon_surfaces(view->id);

	xcb_ewmh_get_wm_icon_reply_t icon_reply = {0};
	if (!xwayland_surface_fetch_icon(view->xwayland_surface, &icon_reply)) {
		goto out;
	}

	xcb_ewmh_wm_icon_iterator_t iter = xcb_ewmh_get_wm_icon_iterator(&icon_reply);
	for (; iter.rem; xcb_ewmh_get_wm_icon_next(&iter)) {
		cairo_surface_t *surface =
			cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
				iter.width, iter.height);
		uint8_t *dst = cairo_image_surface_get_data(surface);
		int dst_stride = cairo_format_stride_for_width(
			CAIRO_FORMAT_ARGB32, iter.width);

		/* Pre-multiply alpha */
		for (uint32_t y = 0; y < iter.height; y++) {
			for (uint32_t x = 0; x < iter.width; x++) {
				uint32_t i = x + y * iter.width;
				uint8_t *src_pixel = (uint8_t *)&iter.data[i];
				uint8_t *dst_pixel = &dst[x * 4 + y * dst_stride];
				dst_pixel[0] = src_pixel[0] * src_pixel[3] / 255;
				dst_pixel[1] = src_pixel[1] * src_pixel[3] / 255;
				dst_pixel[2] = src_pixel[2] * src_pixel[3] / 255;
				dst_pixel[3] = src_pixel[3];
			}
		}

		cairo_surface_mark_dirty(surface);
		view_add_icon_surface(view->id, surface);
	}

out:
	view_update_icon(view->id);
	xcb_ewmh_get_wm_icon_reply_wipe(&icon_reply);
}

static void
handle_focus_in(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, focus_in);
	struct wlr_surface *surface = view->xwayland_surface->surface;

	if (!surface) {
		/*
		 * It is rare but possible for the focus_in event to be
		 * received before the map event. This has been seen
		 * during CLion startup, when focus is initially offered
		 * to the splash screen but accepted later by the main
		 * window instead. (In this case, the focus transfer is
		 * client-initiated but allowed by wlroots because the
		 * same PID owns both windows.)
		 *
		 * Set a flag to record this condition, and update the
		 * seat focus later when the view is actually mapped.
		 */
		wlr_log(WLR_DEBUG, "focus_in received before map");
		view->focused_before_map = true;
		return;
	}

	if (surface != g_seat.wlr_seat->keyboard_state.focused_surface) {
		seat_focus_surface(surface);
	}
}

/*
 * Sets the initial geometry of maximized/fullscreen views before
 * actually mapping them, so that they can do their initial layout and
 * drawing with the correct geometry. This avoids visual glitches and
 * also avoids undesired layout changes with some apps (e.g. HomeBank).
 */
static void
handle_map_request(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, map_request);
	struct xwayland_surface *xsurface = view->xwayland_surface;

	if (view->st->mapped) {
		/* Probably shouldn't happen, but be sure */
		return;
	}

	/*
	 * Per the Extended Window Manager Hints (EWMH) spec: "The Window
	 * Manager SHOULD honor _NET_WM_STATE whenever a withdrawn window
	 * requests to be mapped."
	 */
	view_fullscreen(view->id, xsurface->fullscreen, /* output */ NULL);
	enum view_axis axis = VIEW_AXIS_NONE;
	if (xsurface->maximized_horz) {
		axis |= VIEW_AXIS_HORIZONTAL;
	}
	if (xsurface->maximized_vert) {
		axis |= VIEW_AXIS_VERTICAL;
	}
	view_maximize(view->id, axis);
	view_set_always_on_top(view->id, xsurface->above);
}

/* for unmanaged surface only */
static void
handle_set_geometry(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, set_geometry);
	if (view->node) {
		wlr_scene_node_set_position(view->node,
			view->xwayland_surface->x, view->xwayland_surface->y);
		cursor_update_focus();
	}
}

static void
map_unmanaged_surface(struct view *view)
{
	assert(!view->node);
	struct xwayland_surface *xsurface = view->xwayland_surface;

	/* Stack new surface on top */
	wl_list_append(&g_server.unmanaged_surfaces, &view->link);

	CONNECT_SIGNAL(xsurface, view, set_geometry);

	if (xwayland_surface_override_redirect_wants_focus(xsurface)
			|| view->ever_grabbed_focus) {
		seat_focus_surface(xsurface->surface);
	}

	view->node = &wlr_scene_surface_create(g_server.unmanaged_tree,
		xsurface->surface)->buffer->node;
	die_if_null(view->node);

	wlr_scene_node_set_position(view->node, xsurface->x, xsurface->y);
	cursor_update_focus();
}

static void
handle_map(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, map);
	struct xwayland_surface *xwayland_surface = view->xwayland_surface;
	assert(xwayland_surface);
	assert(xwayland_surface->surface);

	if (!view->id) {
		map_unmanaged_surface(view);
		return;
	}

	assert(view->st && !view->st->mapped);

	/*
	 * The map_request event may not be received when an unmanaged
	 * (override-redirect) surface becomes managed. To make sure we
	 * have valid geometry in that case, call handle_map_request()
	 * explicitly (calling it twice is harmless).
	 */
	handle_map_request(&view->map_request, NULL);

	/*
	 * If the view was focused (on the xwayland server side) before
	 * being mapped, update the seat focus now. Note that this only
	 * really matters in the case of Globally Active input windows.
	 * In all other cases, it's redundant since view_impl_map()
	 * results in the view being focused anyway.
	 */
	if (view->focused_before_map) {
		view->focused_before_map = false;
		seat_focus_surface(view->xwayland_surface->surface);
	}

	view_map_common(view->id, xwayland_view_focus_mode(view));
	if (xwayland_view_has_strut_partial(view)) {
		output_update_all_usable_areas(false);
	}
}

static void
focus_next_surface(struct xwayland_surface *xsurface)
{
	/* Try to focus on last created unmanaged xwayland surface */
	struct view *u;
	struct wl_list *list = &g_server.unmanaged_surfaces;
	wl_list_for_each_reverse(u, list, link) {
		struct xwayland_surface *prev = u->xwayland_surface;
		if (xwayland_surface_override_redirect_wants_focus(prev)
				|| u->ever_grabbed_focus) {
			seat_focus_surface(prev->surface);
			return;
		}
	}

	/*
	 * Unmanaged surfaces do not clear the active view when mapped.
	 * Therefore, we can simply give the focus back to the active
	 * view when the last unmanaged surface is unmapped.
	 */
	view_refocus_active();
}

static void
unmap_unmanaged_surface(struct view *view)
{
	assert(view->node);
	struct xwayland_surface *xsurface = view->xwayland_surface;

	wl_list_remove(&view->link);
	wl_list_remove(&view->set_geometry.link);

	/*
	 * Destroy the scene node. It would get destroyed later when
	 * the wlr_surface is destroyed, but if the unmanaged surface
	 * gets converted to a managed surface, that may be a while.
	 */
	wlr_scene_node_destroy(view->node);
	view->node = NULL;

	cursor_update_focus();

	if (g_seat.wlr_seat->keyboard_state.focused_surface == xsurface->surface) {
		focus_next_surface(xsurface);
	}
}

static void
handle_unmap(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, unmap);

	if (!view->id) {
		unmap_unmanaged_surface(view);
		return;
	}

	assert(view->st && view->st->mapped);

	view_unmap_common(view->id);
	if (xwayland_view_has_strut_partial(view)) {
		output_update_all_usable_areas(false);
	}
}

void
xwayland_view_maximize(struct view *view, enum view_axis maximized)
{
	xwayland_surface_set_maximized(xwayland_surface_from_view(view),
		maximized & VIEW_AXIS_HORIZONTAL, maximized & VIEW_AXIS_VERTICAL);
}

void
xwayland_view_minimize(struct view *view, bool minimized)
{
	struct xwayland_surface *xsurface = xwayland_surface_from_view(view);
	xwayland_surface_set_minimized(xsurface, minimized);
	if (minimized) {
		xwayland_surface_restack(xsurface, NULL, XCB_STACK_MODE_BELOW);
	}
}

ViewId
xwayland_view_get_root_id(struct view *view)
{
	struct xwayland_surface *root = top_parent_of(view);

	/*
	 * The case of root->data == NULL is unlikely, but has been reported
	 * when starting XWayland games (for example 'Fall Guys'). It is
	 * believed to be caused by setting override-redirect on the root
	 * xwayland_surface making it not be associated with a view anymore.
	 */
	return (root && root->data) ? (ViewId)root->data : view->id;
}

bool
xwayland_view_is_modal_dialog(struct view *self)
{
	return xwayland_surface_from_view(self)->modal;
}

void
xwayland_view_set_active(struct view *view, bool active)
{
	xwayland_surface_activate(xwayland_surface_from_view(view), active);
}

void
xwayland_view_set_fullscreen(struct view *view, bool fullscreen)
{
	xwayland_surface_set_fullscreen(xwayland_surface_from_view(view),
		fullscreen);
}

static void
xwayland_view_create(struct xwayland_surface *xsurface, bool mapped)
{
	struct view *view = znew(*view);
	view_init(view, /* is_xwayland */ true);

	/*
	 * Set two-way view <-> xsurface association.  Usually the association
	 * remains until the xsurface is destroyed (which also destroys the
	 * view).  The only exception is caused by setting override-redirect on
	 * the xsurface, which removes it from the view (destroying the view)
	 * and makes it an "unmanaged" surface.
	 */
	view->xwayland_surface = xsurface;
	xsurface->data = (void *)view->id;

	CONNECT_SIGNAL(xsurface, view, destroy);
	CONNECT_SIGNAL(xsurface, view, request_minimize);
	CONNECT_SIGNAL(xsurface, view, request_maximize);
	CONNECT_SIGNAL(xsurface, view, request_fullscreen);
	CONNECT_SIGNAL(xsurface, view, request_move);
	CONNECT_SIGNAL(xsurface, view, request_resize);
	CONNECT_SIGNAL(xsurface, view, set_title);

	/* Events specific to XWayland views */
	CONNECT_SIGNAL(xsurface, view, associate);
	CONNECT_SIGNAL(xsurface, view, dissociate);
	CONNECT_SIGNAL(xsurface, view, request_above);
	CONNECT_SIGNAL(xsurface, view, request_activate);
	CONNECT_SIGNAL(xsurface, view, request_configure);
	CONNECT_SIGNAL(xsurface, view, set_class);
	CONNECT_SIGNAL(xsurface, view, set_decorations);
	CONNECT_SIGNAL(xsurface, view, set_override_redirect);
	CONNECT_SIGNAL(xsurface, view, set_strut_partial);
	CONNECT_SIGNAL(xsurface, view, set_window_type);
	CONNECT_SIGNAL(xsurface, view, set_icon);
	CONNECT_SIGNAL(xsurface, view, focus_in);
	CONNECT_SIGNAL(xsurface, view, map_request);

	if (xsurface->surface) {
		handle_associate(&view->associate, NULL);
	}
	if (mapped) {
		handle_map(&view->map, NULL);
	}
}

/* for unmanaged surface only */
static void
handle_grab_focus(struct wl_listener *listener, void *data)
{
	struct view *view = wl_container_of(listener, view, grab_focus);

	view->ever_grabbed_focus = true;
	if (view->node) {
		assert(view->xwayland_surface->surface);
		seat_focus_surface(view->xwayland_surface->surface);
	}
}

static void
xwayland_unmanaged_create(struct xwayland_surface *xsurface, bool mapped)
{
	struct view *view = znew(*view);
	view->xwayland_surface = xsurface;
	/*
	 * xsurface->data is presumed to be a ViewId if set,
	 * so it must be left NULL for an unmanaged surface (it should
	 * be NULL already at this point).
	 */
	assert(!xsurface->data);

	CONNECT_SIGNAL(xsurface, view, associate);
	CONNECT_SIGNAL(xsurface, view, dissociate);
	CONNECT_SIGNAL(xsurface, view, destroy);
	CONNECT_SIGNAL(xsurface, view, grab_focus);
	CONNECT_SIGNAL(xsurface, view, request_activate);
	CONNECT_SIGNAL(xsurface, view, request_configure);
	CONNECT_SIGNAL(xsurface, view, set_override_redirect);

	if (xsurface->surface) {
		handle_associate(&view->associate, NULL);
	}
	if (mapped) {
		handle_map(&view->map, NULL);
	}
}

static void
handle_new_surface(struct wl_listener *listener, void *data)
{
	struct xwayland_surface *xsurface = data;

	/*
	 * We do not create 'views' for xwayland override_redirect surfaces,
	 * but add them to server.unmanaged_surfaces so that we can render them
	 */
	if (xsurface->override_redirect) {
		xwayland_unmanaged_create(xsurface, /* mapped */ false);
	} else {
		xwayland_view_create(xsurface, /* mapped */ false);
	}
}

static void
handle_server_ready(struct wl_listener *listener, void *data)
{
	/* Fire an Xwayland startup script if one (or many) can be found */
	session_run_script("xinitrc");
}

static void
handle_xwm_ready(struct wl_listener *listener, void *data)
{
	xwayland_set_seat(g_server.xwayland, g_seat.wlr_seat);
	xwayland_update_workarea();
}

void
xwayland_server_init(struct wlr_compositor *compositor)
{
	g_server.xwayland = xwayland_create(
		g_server.wl_display, compositor, /* lazy */ false);
	if (!g_server.xwayland) {
		wlr_log(WLR_ERROR, "cannot create xwayland server");
		exit(EXIT_FAILURE);
	}
	g_server.xwayland_new_surface.notify = handle_new_surface;
	wl_signal_add(&g_server.xwayland->events.new_surface,
		&g_server.xwayland_new_surface);

	g_server.xwayland_server_ready.notify = handle_server_ready;
	wl_signal_add(&g_server.xwayland->server->events.ready,
		&g_server.xwayland_server_ready);

	g_server.xwayland_xwm_ready.notify = handle_xwm_ready;
	wl_signal_add(&g_server.xwayland->events.ready,
		&g_server.xwayland_xwm_ready);

	if (setenv("DISPLAY", g_server.xwayland->display_name, true) < 0) {
		wlr_log_errno(WLR_ERROR, "unable to set DISPLAY for xwayland");
	} else {
		wlr_log(WLR_DEBUG, "xwayland is running on display %s",
			g_server.xwayland->display_name);
	}

	struct wlr_xcursor *xcursor;
	xcursor = wlr_xcursor_manager_get_xcursor(
		g_seat.xcursor_manager, XCURSOR_DEFAULT, 1);
	if (xcursor) {
		struct wlr_xcursor_image *image = xcursor->images[0];
		xwayland_set_cursor(g_server.xwayland, image->buffer,
			image->width * 4, image->width,
			image->height, image->hotspot_x,
			image->hotspot_y);
	}
}

void
xwayland_reset_cursor(void)
{
	/*
	 * As xwayland caches the pixel data when not yet started up
	 * due to the delayed lazy startup approach, we do have to
	 * re-set the xwayland cursor image. Otherwise the first X11
	 * client connected will cause the xwayland server to use
	 * the cached (and potentially destroyed) pixel data.
	 *
	 * Calling this function after reloading the cursor theme
	 * ensures that the cached pixel data keeps being valid.
	 *
	 * To reproduce:
	 * - Compile with b_sanitize=address,undefined
	 * - Start labwc (nothing in autostart that could create
	 *   a X11 connection, e.g. no GTK or X11 application)
	 * - Reconfigure
	 * - Start some X11 client
	 */

	if (!g_server.xwayland) {
		return;
	}

	struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
		g_seat.xcursor_manager, XCURSOR_DEFAULT, 1);

	if (xcursor && !g_server.xwayland->xwm) {
		/* Prevents setting the cursor on an active xwayland server */
		struct wlr_xcursor_image *image = xcursor->images[0];
		xwayland_set_cursor(g_server.xwayland, image->buffer,
			image->width * 4, image->width,
			image->height, image->hotspot_x,
			image->hotspot_y);
		return;
	}

	if (g_server.xwayland->cursor) {
		/*
		 * The previous configured theme has set the
		 * default cursor or the xwayland server is
		 * currently running but still has a cached
		 * xcursor set that will be used on the next
		 * xwayland destroy -> lazy startup cycle.
		 */
		zfree(g_server.xwayland->cursor);
	}
}

void
xwayland_server_finish(void)
{
	struct xwayland *xwayland = g_server.xwayland;
	wl_list_remove(&g_server.xwayland_new_surface.link);
	wl_list_remove(&g_server.xwayland_server_ready.link);
	wl_list_remove(&g_server.xwayland_xwm_ready.link);

	/*
	 * Reset g_server.xwayland to NULL first to prevent callbacks (like
	 * server_global_filter) from accessing it as it is destroyed
	 */
	g_server.xwayland = NULL;
	xwayland_destroy(xwayland);
}

static bool
intervals_overlap(int start_a, int end_a, int start_b, int end_b)
{
	/* check for empty intervals */
	if (end_a <= start_a || end_b <= start_b) {
		return false;
	}

	return start_a < start_b ?
		start_b < end_a :  /* B starts within A */
		start_a < end_b;   /* A starts within B */
}

/*
 * Subtract the area of an XWayland view (e.g. panel) from the usable
 * area of the output based on _NET_WM_STRUT_PARTIAL property.
 */
void
xwayland_view_adjust_usable_area(struct view *view, struct output *output)
{
	assert(view);
	assert(output);

	if (!view->xwayland_surface) {
		return;
	}

	xcb_ewmh_wm_strut_partial_t *strut =
		xwayland_surface_from_view(view)->strut_partial;
	if (!strut) {
		return;
	}

	/* these are layout coordinates */
	struct wlr_box lb = { 0 };
	wlr_output_layout_get_box(g_server.output_layout, NULL, &lb);
	struct wlr_box ob = { 0 };
	wlr_output_layout_get_box(g_server.output_layout, output->wlr_output, &ob);

	/*
	 * strut->right/bottom are offsets from the lower right corner
	 * of the X11 screen, which should generally correspond with the
	 * lower right corner of the output layout
	 */
	double strut_left = strut->left;
	double strut_right = (lb.x + lb.width) - strut->right;
	double strut_top = strut->top;
	double strut_bottom = (lb.y + lb.height) - strut->bottom;

	/* convert layout to output coordinates */
	wlr_output_layout_output_coords(g_server.output_layout,
		output->wlr_output, &strut_left, &strut_top);
	wlr_output_layout_output_coords(g_server.output_layout,
		output->wlr_output, &strut_right, &strut_bottom);

	struct wlr_box *usable = &output->usable_area;
	/* deal with right/bottom rather than width/height */
	int usable_right = usable->x + usable->width;
	int usable_bottom = usable->y + usable->height;

	/* here we mix output and layout coordinates; be careful */
	if (strut_left > usable->x && strut_left < usable_right
			&& intervals_overlap(ob.y, ob.y + ob.height,
			strut->left_start_y, strut->left_end_y + 1)) {
		usable->x = strut_left;
	}
	if (strut_right > usable->x && strut_right < usable_right
			&& intervals_overlap(ob.y, ob.y + ob.height,
			strut->right_start_y, strut->right_end_y + 1)) {
		usable_right = strut_right;
	}
	if (strut_top > usable->y && strut_top < usable_bottom
			&& intervals_overlap(ob.x, ob.x + ob.width,
			strut->top_start_x, strut->top_end_x + 1)) {
		usable->y = strut_top;
	}
	if (strut_bottom > usable->y && strut_bottom < usable_bottom
			&& intervals_overlap(ob.x, ob.x + ob.width,
			strut->bottom_start_x, strut->bottom_end_x + 1)) {
		usable_bottom = strut_bottom;
	}

	usable->width = usable_right - usable->x;
	usable->height = usable_bottom - usable->y;
}

void
xwayland_update_workarea(void)
{
	/*
	 * Do nothing if called during destroy or before xwayland is ready.
	 * This function will be called again from the ready signal handler.
	 */
	if (!g_server.xwayland || !g_server.xwayland->xwm) {
		return;
	}

	struct wlr_box lb;
	wlr_output_layout_get_box(g_server.output_layout, NULL, &lb);

	/* Compute outer edges of layout (excluding negative regions) */
	int layout_left = MAX(0, lb.x);
	int layout_right = MAX(0, lb.x + lb.width);
	int layout_top = MAX(0, lb.y);
	int layout_bottom = MAX(0, lb.y + lb.height);

	/* Workarea is initially the entire layout */
	int workarea_left = layout_left;
	int workarea_right = layout_right;
	int workarea_top = layout_top;
	int workarea_bottom = layout_bottom;

	struct output *output;
	wl_list_for_each(output, &g_server.outputs, link) {
		if (!output_is_usable(output)) {
			continue;
		}

		struct wlr_box ob;
		wlr_output_layout_get_box(g_server.output_layout,
			output->wlr_output, &ob);

		/* Compute edges of output */
		int output_left = ob.x;
		int output_right = ob.x + ob.width;
		int output_top = ob.y;
		int output_bottom = ob.y + ob.height;

		/* Compute edges of usable area */
		int usable_left = output_left + output->usable_area.x;
		int usable_right = usable_left + output->usable_area.width;
		int usable_top = output_top + output->usable_area.y;
		int usable_bottom = usable_top + output->usable_area.height;

		/*
		 * Only adjust workarea edges for output edges that are
		 * aligned with outer edges of layout
		 */
		if (output_left == layout_left) {
			workarea_left = MAX(workarea_left, usable_left);
		}
		if (output_right == layout_right) {
			workarea_right = MIN(workarea_right, usable_right);
		}
		if (output_top == layout_top) {
			workarea_top = MAX(workarea_top, usable_top);
		}
		if (output_bottom == layout_bottom) {
			workarea_bottom = MIN(workarea_bottom, usable_bottom);
		}
	}

	/*
	 * Set _NET_WORKAREA property. We don't report virtual desktops
	 * to XWayland, so we set only one workarea.
	 */
	struct wlr_box workarea = {
		.x = workarea_left,
		.y = workarea_top,
		.width = workarea_right - workarea_left,
		.height = workarea_bottom - workarea_top,
	};
	xwayland_set_workareas(g_server.xwayland, &workarea, 1);
}
