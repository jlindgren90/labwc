// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "xwayland.h"
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/xwayland.h>
#include "buffer.h"
#include "common/array.h"
#include "common/macros.h"
#include "common/mem.h"
#include "config/rcxml.h"
#include "config/session.h"
#include "foreign-toplevel/foreign.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "view.h"
#include "view-impl-common.h"
#include "window-rules.h"
#include "workspaces.h"

enum atoms {
	ATOM_NET_WM_ICON = 0,

	ATOM_COUNT,
};

static const char *const atom_names[] = {
	"_NET_WM_ICON",
};

static_assert(ARRAY_SIZE(atom_names) == ATOM_COUNT, "atom names out of sync");

static xcb_atom_t atoms[ATOM_COUNT] = {0};

bool
xwayland_view::contains_window_type(lab_window_type window_type)
{
	/* Compile-time check that the enum types match */
	static_assert(LAB_WINDOW_TYPE_DESKTOP ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DESKTOP
		&& LAB_WINDOW_TYPE_DOCK ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DOCK
		&& LAB_WINDOW_TYPE_TOOLBAR ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLBAR
		&& LAB_WINDOW_TYPE_MENU ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_MENU
		&& LAB_WINDOW_TYPE_UTILITY ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY
		&& LAB_WINDOW_TYPE_SPLASH ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH
		&& LAB_WINDOW_TYPE_DIALOG ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG
		&& LAB_WINDOW_TYPE_DROPDOWN_MENU ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DROPDOWN_MENU
		&& LAB_WINDOW_TYPE_POPUP_MENU ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_POPUP_MENU
		&& LAB_WINDOW_TYPE_TOOLTIP ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_TOOLTIP
		&& LAB_WINDOW_TYPE_NOTIFICATION ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_NOTIFICATION
		&& LAB_WINDOW_TYPE_COMBO ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_COMBO
		&& LAB_WINDOW_TYPE_DND ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DND
		&& LAB_WINDOW_TYPE_NORMAL ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_NORMAL
		&& LAB_WINDOW_TYPE_LEN ==
			(int)WLR_XWAYLAND_NET_WM_WINDOW_TYPE_NORMAL + 1,
		"lab_window_type does not match wlr_xwayland_net_wm_window_type");

	return wlr_xwayland_surface_has_window_type(xwayland_surface,
		(enum wlr_xwayland_net_wm_window_type)window_type);
}

view_size_hints
xwayland_view::get_size_hints()
{
	auto hints = xwayland_surface->size_hints;
	if (!hints) {
		return {};
	}
	return {
		.min_width = hints->min_width,
		.min_height = hints->min_height,
		.width_inc = hints->width_inc,
		.height_inc = hints->height_inc,
		.base_width = hints->base_width,
		.base_height = hints->base_height,
	};
}

enum view_wants_focus
xwayland_view::wants_focus()
{
	auto xsurface = xwayland_surface;
	switch (wlr_xwayland_surface_icccm_input_model(xsurface)) {
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
		return VIEW_WANTS_FOCUS_ALWAYS;

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
		return (wlr_xwayland_surface_has_window_type(xsurface,
				WLR_XWAYLAND_NET_WM_WINDOW_TYPE_NORMAL)
			|| wlr_xwayland_surface_has_window_type(xsurface,
				WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG)) ?
			VIEW_WANTS_FOCUS_LIKELY : VIEW_WANTS_FOCUS_UNLIKELY;

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

	return VIEW_WANTS_FOCUS_NEVER;
}

bool
xwayland_view::has_strut_partial()
{
	return (bool)xwayland_surface->strut_partial;
}

void
xwayland_view::offer_focus()
{
	wlr_xwayland_surface_offer_focus(xwayland_surface);
}

static struct wlr_xwayland_surface *
top_parent_of(struct view *view)
{
	struct wlr_xwayland_surface *s = xwayland_surface_from_view(view);
	while (s->parent) {
		s = s->parent;
	}
	return s;
}

static struct xwayland_view *
xwayland_view_from_view(struct view *view)
{
	assert(view->type == LAB_XWAYLAND_VIEW);
	return (struct xwayland_view *)view;
}

struct wlr_xwayland_surface *
xwayland_surface_from_view(struct view *view)
{
	struct xwayland_view *xwayland_view = xwayland_view_from_view(view);
	assert(xwayland_view->xwayland_surface);
	return xwayland_view->xwayland_surface;
}

static void
ensure_initial_geometry_and_output(xwayland_view *view)
{
	if (wlr_box_empty(&view->current)) {
		view->current.x = view->xwayland_surface->x;
		view->current.y = view->xwayland_surface->y;
		view->current.width = view->xwayland_surface->width;
		view->current.height = view->xwayland_surface->height;
		/*
		 * If there is no pending move/resize yet, then set
		 * current values (used in map()).
		 */
		if (wlr_box_empty(&view->pending)) {
			view->pending = view->current;
		}
	}
	if (!view->output) {
		/*
		 * Just use the cursor output since we don't know yet
		 * whether the surface position is meaningful.
		 */
		view_set_output(view, output_nearest_to_cursor());
	}
}

static bool
want_deco(xwayland_view *view)
{
	/* Window-rules take priority if they exist for this view */
	switch (window_rules_get_property(view, "serverDecoration")) {
	case LAB_PROP_TRUE:
		return true;
	case LAB_PROP_FALSE:
		return false;
	default:
		break;
	}

	return view->xwayland_surface->decorations
		== WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
}

void
xwayland_view::handle_commit(void *data)
{
	auto view = this;
	assert(data && data == view->surface);

	/* Must receive commit signal before accessing surface->current* */
	struct wlr_surface_state *state = &view->surface->current;
	struct wlr_box *current = &view->current;

	/*
	 * If there is a pending move/resize, wait until the surface
	 * size changes to update geometry. The hope is to update both
	 * the position and the size of the view at the same time,
	 * reducing visual glitches.
	 */
	if (current->width != state->width || current->height != state->height) {
		view_impl_apply_geometry(view, state->width, state->height);
	}
}

void
xwayland_view::handle_request_move(void *)
{
	/*
	 * This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check
	 * the provided serial against a list of button press serials sent to
	 * this client, to prevent the client from requesting this whenever they
	 * want.
	 */
	if (this == g_seat.pressed.view) {
		interactive_begin(this, LAB_INPUT_STATE_MOVE, LAB_EDGE_NONE);
	}
}

void
xwayland_view::handle_request_resize(void *data)
{
	/*
	 * This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check
	 * the provided serial against a list of button press serials sent to
	 * this client, to prevent the client from requesting this whenever they
	 * want.
	 */
	auto event = (wlr_xwayland_resize_event *)data;
	if (this == g_seat.pressed.view) {
		interactive_begin(this, LAB_INPUT_STATE_RESIZE, event->edges);
	}
}

void
xwayland_view::handle_associate(void *)
{
	assert(xwayland_surface->surface);

	CONNECT_LISTENER(xwayland_surface->surface, this, map);
	CONNECT_LISTENER(xwayland_surface->surface, this, unmap);
}

void
xwayland_view::handle_dissociate(void *)
{
	on_map.disconnect();
	on_unmap.disconnect();
}

void
xwayland_view::handle_surface_destroy(void *data)
{
	assert(data && data == this->surface);

	this->surface = NULL;
	on_surface_destroy.disconnect();
}

xwayland_view::~xwayland_view()
{
	assert(xwayland_surface->data == this);

	/*
	 * Break view <-> xsurface association.  Note that the xsurface
	 * may not actually be destroyed at this point; it may become an
	 * "unmanaged" surface instead (in that case it is important
	 * that xsurface->data not point to the destroyed view).
	 */
	xwayland_surface->data = NULL;
}

void
xwayland_view::configure(wlr_box geo)
{
	auto view = this;
	view->pending = geo;
	wlr_xwayland_surface_configure(view->xwayland_surface, geo.x, geo.y,
		geo.width, geo.height);

	/*
	 * For unknown reasons, XWayland surfaces that are completely
	 * offscreen seem not to generate commit events. In rare cases,
	 * this can prevent an offscreen window from moving onscreen
	 * (since we wait for a commit event that never occurs). As a
	 * workaround, move offscreen surfaces immediately.
	 */
	bool is_offscreen =
		!wlr_box_empty(&view->current)
		&& !wlr_output_layout_intersects(g_server.output_layout, NULL,
			&view->current);

	/* If not resizing, process the move immediately */
	if (is_offscreen || (view->current.width == geo.width
			&& view->current.height == geo.height)) {
		view->current.x = geo.x;
		view->current.y = geo.y;
		view_moved(view);
	}
}

void
xwayland_view::handle_request_configure(void *data)
{
	auto view = this;
	auto event = (wlr_xwayland_surface_configure_event *)data;
	bool ignore_configure_requests = window_rules_get_property(
		view, "ignoreConfigureRequest") == LAB_PROP_TRUE;

	if (view_is_floating(view) && !ignore_configure_requests) {
		/* Honor client configure requests for floating views */
		struct wlr_box box = {.x = event->x, .y = event->y,
			.width = event->width, .height = event->height};
		view_adjust_size(view, &box.width, &box.height);
		view->configure(box);
	} else {
		/*
		 * Do not allow clients to request geometry other than
		 * what we computed for maximized/fullscreen/tiled
		 * views. Ignore the client request and send back a
		 * ConfigureNotify event with the computed geometry.
		 */
		view->configure(view->pending);
	}
}

void
xwayland_view::handle_request_activate(void *)
{
	if (window_rules_get_property(this, "ignoreFocusRequest") == LAB_PROP_TRUE) {
		wlr_log(WLR_INFO, "Ignoring focus request due to window rule configuration");
		return;
	}

	desktop_focus_view(this, /*raise*/ true);
}

void
xwayland_view::handle_request_minimize(void *data)
{
	auto event = (wlr_xwayland_minimize_event *)data;
	view_minimize(this, event->minimize);
}

void
xwayland_view::handle_request_maximize(void *)
{
	auto view = this;
	if (!view->mapped) {
		ensure_initial_geometry_and_output(view);
		/*
		 * Set decorations early to avoid changing geometry
		 * after maximize (reduces visual glitches).
		 */
		if (want_deco(view)) {
			view_set_ssd_mode(view, LAB_SSD_MODE_FULL);
		} else {
			view_set_ssd_mode(view, LAB_SSD_MODE_NONE);
		}
	}

	enum view_axis maximize = VIEW_AXIS_NONE;
	if (view->xwayland_surface->maximized_vert) {
		maximize = (view_axis)(maximize | VIEW_AXIS_VERTICAL);
	}
	if (view->xwayland_surface->maximized_horz) {
		maximize = (view_axis)(maximize | VIEW_AXIS_HORIZONTAL);
	}
	view_maximize(view, maximize, /*store_natural_geometry*/ true);
}

void
xwayland_view::handle_request_fullscreen(void *)
{
	bool fullscreen = xwayland_surface->fullscreen;
	if (!this->mapped) {
		ensure_initial_geometry_and_output(this);
	}
	view_set_fullscreen(this, fullscreen);
}

void
xwayland_view::handle_set_title(void *)
{
	view_update_title(this);
}

void
xwayland_view::handle_set_class(void *)
{
	view_update_app_id(this);
}

void
xwayland_view::close()
{
	wlr_xwayland_surface_close(xwayland_surface);
}

const char *
xwayland_view::get_string_prop(const char *prop)
{
	if (!strcmp(prop, "title")) {
		return xwayland_surface->title ? xwayland_surface->title : "";
	}
	if (!strcmp(prop, "class")) {
		return xwayland_surface->class_ ? xwayland_surface->class_ : "";
	}
	/*
	 * Use the WM_CLASS 'instance' (1st string) for the app_id. Per
	 * ICCCM, this is usually "the trailing part of the name used to
	 * invoke the program (argv[0] stripped of any directory names)".
	 *
	 * In most cases, the 'class' (2nd string) is the same as the
	 * 'instance' except for being capitalized. We want lowercase
	 * here since we use the app_id for icon lookups.
	 */
	if (!strcmp(prop, "app_id")) {
		return xwayland_surface->instance ? xwayland_surface->instance : "";
	}
	return "";
}

void
xwayland_view::handle_set_decorations(void *)
{
	if (want_deco(this)) {
		view_set_ssd_mode(this, LAB_SSD_MODE_FULL);
	} else {
		view_set_ssd_mode(this, LAB_SSD_MODE_NONE);
	}
}

void
xwayland_view::handle_set_override_redirect(void *)
{
	auto xsurface = xwayland_surface;
	bool mapped = xsurface->surface && xsurface->surface->mapped;
	if (mapped) {
		unmap(/* client_request */ true);
	}
	delete this;
	/* "this" is invalid after this point */
	xwayland_unmanaged_create(xsurface, mapped);
}

void
xwayland_view::handle_set_strut_partial(void *)
{
	if (this->mapped) {
		output_update_all_usable_areas(/* layout_changed */ false);
	}
}

static void
update_icon(struct xwayland_view *view)
{
	auto window_id = view->xwayland_surface->window_id;

	xcb_connection_t *xcb_conn =
		wlr_xwayland_get_xwm_connection(g_server.xwayland);
	xcb_get_property_cookie_t cookie = xcb_get_property(xcb_conn, 0,
		window_id, atoms[ATOM_NET_WM_ICON], XCB_ATOM_CARDINAL, 0, 0x10000);
	xcb_get_property_reply_t *reply = xcb_get_property_reply(xcb_conn, cookie, NULL);
	if (!reply) {
		return;
	}
	xcb_ewmh_get_wm_icon_reply_t icon;
	if (!xcb_ewmh_get_wm_icon_from_reply(&icon, reply)) {
		wlr_log(WLR_INFO, "Invalid x11 icon");
		view_set_icon(view, NULL, {});
		goto out;
	}

{ /* !goto */
	xcb_ewmh_wm_icon_iterator_t iter = xcb_ewmh_get_wm_icon_iterator(&icon);
	reflist<lab_data_buffer> buffers;
	for (; iter.rem; xcb_ewmh_get_wm_icon_next(&iter)) {
		auto buf = make_u8_array(4 * iter.width * iter.height);

		/* Pre-multiply alpha */
		for (uint32_t y = 0; y < iter.height; y++) {
			for (uint32_t x = 0; x < iter.width; x++) {
				uint32_t i = x + y * iter.width;
				uint8_t *src_pixel = (uint8_t *)&iter.data[i];
				uint8_t *dst_pixel = &buf[4 * i];
				dst_pixel[0] = src_pixel[0] * src_pixel[3] / 255;
				dst_pixel[1] = src_pixel[1] * src_pixel[3] / 255;
				dst_pixel[2] = src_pixel[2] * src_pixel[3] / 255;
				dst_pixel[3] = src_pixel[3];
			}
		}

		buffers.append(buffer_create_from_data(std::move(buf),
			iter.width, iter.height, 4 * iter.width));
	}

	/* view takes ownership of the buffers */
	view_set_icon(view, NULL, std::move(buffers));
} out:
	free(reply);
}

void
xwayland_view::handle_focus_in(void *)
{
	auto view = this;
	if (!view->surface) {
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

	if (view->surface != g_seat.seat->keyboard_state.focused_surface) {
		seat_focus_surface(view->surface);
	}
}

/*
 * Sets the initial geometry of maximized/fullscreen views before
 * actually mapping them, so that they can do their initial layout and
 * drawing with the correct geometry. This avoids visual glitches and
 * also avoids undesired layout changes with some apps (e.g. HomeBank).
 */
void
xwayland_view::handle_map_request(void *)
{
	auto view = this;
	auto xsurface = view->xwayland_surface;

	if (view->mapped) {
		/* Probably shouldn't happen, but be sure */
		return;
	}

	/* Keep the view invisible until actually mapped */
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	ensure_initial_geometry_and_output(view);

	/*
	 * Per the Extended Window Manager Hints (EWMH) spec: "The Window
	 * Manager SHOULD honor _NET_WM_STATE whenever a withdrawn window
	 * requests to be mapped."
	 *
	 * The following order of operations is intended to reduce the
	 * number of resize (Configure) events:
	 *   1. set fullscreen state
	 *   2. set decorations (depends on fullscreen state)
	 *   3. set maximized (geometry depends on decorations)
	 */
	view_set_fullscreen(view, xsurface->fullscreen);
	if (!view->been_mapped) {
		if (want_deco(view)) {
			view_set_ssd_mode(view, LAB_SSD_MODE_FULL);
		} else {
			view_set_ssd_mode(view, LAB_SSD_MODE_NONE);
		}
	}
	enum view_axis axis = VIEW_AXIS_NONE;
	if (xsurface->maximized_horz) {
		axis = (view_axis)(axis | VIEW_AXIS_HORIZONTAL);
	}
	if (xsurface->maximized_vert) {
		axis = (view_axis)(axis | VIEW_AXIS_VERTICAL);
	}
	view_maximize(view, axis, /*store_natural_geometry*/ true);
	/*
	 * We could also call set_initial_position() here, but it's not
	 * really necessary until the view is actually mapped (and at
	 * that point the output layout is known for sure).
	 */
}

static void
check_natural_geometry(xwayland_view *view)
{
	int min_width = view_get_min_width();

	/*
	 * Some applications (example: Thonny) don't set a reasonable
	 * un-maximized size when started maximized. Try to detect this
	 * and set a fallback size.
	 */
	if (!view_is_floating(view)
			&& (view->natural_geometry.width < min_width
			|| view->natural_geometry.height < LAB_MIN_VIEW_HEIGHT)) {
		view_set_fallback_natural_geometry(view);
	}
}

static void
set_initial_position(xwayland_view *view)
{
	/* Don't center views with position explicitly specified */
	bool has_position = view->xwayland_surface->size_hints
		&& (view->xwayland_surface->size_hints->flags
			& (XCB_ICCCM_SIZE_HINT_US_POSITION
				| XCB_ICCCM_SIZE_HINT_P_POSITION));

	if (!has_position) {
		view_constrain_size_to_that_of_usable_area(view);

		if (view_is_floating(view)) {
			view_place_by_policy(view,
				/* allow_cursor */ true, rc.placement_policy);
		} else {
			/*
			 * View is maximized/fullscreen. Center the
			 * stored natural geometry without actually
			 * moving the view.
			 */
			view_compute_centered_position(view, NULL,
				view->natural_geometry.width,
				view->natural_geometry.height,
				&view->natural_geometry.x,
				&view->natural_geometry.y);
		}
	}

	/*
	 * Always make sure the view is onscreen and adjusted for any
	 * layout changes that could have occurred between map_request
	 * and the actual map event.
	 */
	view_adjust_for_layout_change(view);
}

static void
init_foreign_toplevel(xwayland_view *view)
{
	assert(!view->foreign_toplevel);
	view->foreign_toplevel = foreign_toplevel_create(view);

	if (!view->xwayland_surface->parent) {
		return;
	}
	auto parent = (struct view *)view->xwayland_surface->parent->data;
	if (!parent || !parent->foreign_toplevel) {
		return;
	}
	foreign_toplevel_set_parent(view->foreign_toplevel, parent->foreign_toplevel);
}

void
xwayland_view::map()
{
	auto view = this;
	if (view->mapped) {
		return;
	}
	if (!xwayland_surface->surface) {
		/*
		 * We may get here if a user minimizes an xwayland dialog at the
		 * same time as the client requests unmap, which xwayland
		 * clients sometimes do without actually requesting destroy
		 * even if they don't intend to use that view/surface anymore
		 */
		wlr_log(WLR_DEBUG, "Cannot map view without wlr_surface");
		return;
	}

	/*
	 * The map_request event may not be received when an unmanaged
	 * (override-redirect) surface becomes managed. To make sure we
	 * have valid geometry in that case, call handle_map_request()
	 * explicitly (calling it twice is harmless).
	 */
	handle_map_request(NULL);

	view->mapped = true;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);

	if (view->surface != xwayland_surface->surface) {
		view->surface = xwayland_surface->surface;

		/* Required to set the surface to NULL when destroyed by the client */
		view->on_surface_destroy.connect(
			&view->surface->events.destroy);

		/* Will be free'd automatically once the surface is being destroyed */
		struct wlr_scene_tree *tree = wlr_scene_subsurface_tree_create(
			view->scene_tree, view->surface);
		die_if_null(tree);

		view->content_tree = tree;
	}

	/*
	 * Exclude unfocusable views from wlr-foreign-toplevel. These
	 * views (notifications, floating toolbars, etc.) should not be
	 * shown in taskbars/docks/etc.
	 */
	if (!view->foreign_toplevel && view_is_focusable(view)) {
		init_foreign_toplevel(view);
		/*
		 * Initial outputs will be synced via
		 * view->events.new_outputs on view_moved()
		 */
	}

	if (!view->been_mapped) {
		check_natural_geometry(view);
		set_initial_position(view);
		/*
		 * When mapping the view for the first time, visual
		 * artifacts are reduced if we display it immediately at
		 * the final intended position/size rather than waiting
		 * for handle_commit().
		 */
		view->current = view->pending;
		view_moved(view);
	}

	/* Add commit here, as xwayland map/unmap can change the wlr_surface */
	CONNECT_LISTENER(xwayland_surface->surface, view, commit);

	/*
	 * If the view was focused (on the xwayland server side) before
	 * being mapped, update the seat focus now. Note that this only
	 * really matters in the case of Globally Active input windows.
	 * In all other cases, it's redundant since view_impl_map()
	 * results in the view being focused anyway.
	 */
	if (view->focused_before_map) {
		view->focused_before_map = false;
		seat_focus_surface(view->surface);
	}

	view_impl_map(view);
	view->been_mapped = true;

	/* Update usable area to account for XWayland "struts" (panels) */
	if (xwayland_surface->strut_partial) {
		output_update_all_usable_areas(false);
	}
}

void
xwayland_view::unmap(bool client_request)
{
	auto view = this;
	if (!view->mapped) {
		goto out;
	}
	view->mapped = false;
	view->on_commit.disconnect();
	wlr_scene_node_set_enabled(&view->scene_tree->node, false);
	view_impl_unmap(view);

	/* Update usable area to account for XWayland "struts" (panels) */
	if (xwayland_surface->strut_partial) {
		output_update_all_usable_areas(false);
	}

	/*
	 * If the view was explicitly unmapped by the client (rather
	 * than just minimized), destroy the foreign toplevel handle so
	 * the unmapped view doesn't show up in panels and the like.
	 */
out:
	if (client_request && view->foreign_toplevel) {
		foreign_toplevel_destroy(view->foreign_toplevel);
		view->foreign_toplevel = NULL;
	}
}

void
xwayland_view::maximize(view_axis maximized)
{
	wlr_xwayland_surface_set_maximized(xwayland_surface,
		maximized & VIEW_AXIS_HORIZONTAL,
		maximized & VIEW_AXIS_VERTICAL);
}

void
xwayland_view::minimize(bool minimized)
{
	wlr_xwayland_surface_set_minimized(xwayland_surface, minimized);
}

view *
xwayland_view::get_root()
{
	struct wlr_xwayland_surface *root = top_parent_of(this);

	/*
	 * The case of root->data == NULL is unlikely, but has been reported
	 * when starting XWayland games (for example 'Fall Guys'). It is
	 * believed to be caused by setting override-redirect on the root
	 * wlr_xwayland_surface making it not be associated with a view anymore.
	 */
	return (root && root->data) ? (struct view *)root->data : this;
}

view_list
xwayland_view::get_children()
{
	view_list children;
	for (auto &view : g_views.reversed()) {
		if (&view == this) {
			continue;
		}
		if (view.type != LAB_XWAYLAND_VIEW) {
			continue;
		}
		/*
		 * This happens when a view has never been mapped or when a
		 * client has requested a `handle_unmap`.
		 */
		if (!view.surface) {
			continue;
		}
		if (!view.mapped && !view.minimized) {
			continue;
		}
		if (top_parent_of(&view) != xwayland_surface) {
			continue;
		}
		children.append(&view);
	}
	return children;
}

bool
xwayland_view::is_modal_dialog()
{
	return xwayland_surface->modal;
}

void
xwayland_view::set_activated(bool activated)
{
	if (activated && xwayland_surface->minimized) {
		wlr_xwayland_surface_set_minimized(xwayland_surface, false);
	}

	wlr_xwayland_surface_activate(xwayland_surface, activated);
}

void
xwayland_view::set_fullscreen(bool fullscreen)
{
	wlr_xwayland_surface_set_fullscreen(xwayland_surface, fullscreen);
}

pid_t
xwayland_view::get_pid()
{
	return xwayland_surface->pid;
}

void
xwayland_view_create(struct wlr_xwayland_surface *xsurface, bool mapped)
{
	ASSERT_PTR(g_server.workspaces.current, workspace);
	auto view = new xwayland_view(xsurface, *workspace);

	/*
	 * Set two-way view <-> xsurface association.  Usually the association
	 * remains until the xsurface is destroyed (which also destroys the
	 * view).  The only exception is caused by setting override-redirect on
	 * the xsurface, which removes it from the view (destroying the view)
	 * and makes it an "unmanaged" surface.
	 */
	xsurface->data = view;

	view->scene_tree = wlr_scene_tree_create(view->workspace->tree);
	node_descriptor_create(&view->scene_tree->node, LAB_NODE_DESC_VIEW, view);

	CONNECT_LISTENER(xsurface, view, destroy);
	CONNECT_LISTENER(xsurface, view, request_minimize);
	CONNECT_LISTENER(xsurface, view, request_maximize);
	CONNECT_LISTENER(xsurface, view, request_fullscreen);
	CONNECT_LISTENER(xsurface, view, request_move);
	CONNECT_LISTENER(xsurface, view, request_resize);
	CONNECT_LISTENER(xsurface, view, set_title);

	/* Events specific to XWayland views */
	CONNECT_LISTENER(xsurface, view, associate);
	CONNECT_LISTENER(xsurface, view, dissociate);
	CONNECT_LISTENER(xsurface, view, request_activate);
	CONNECT_LISTENER(xsurface, view, request_configure);
	CONNECT_LISTENER(xsurface, view, set_class);
	CONNECT_LISTENER(xsurface, view, set_decorations);
	CONNECT_LISTENER(xsurface, view, set_override_redirect);
	CONNECT_LISTENER(xsurface, view, set_strut_partial);
	CONNECT_LISTENER(xsurface, view, focus_in);
	CONNECT_LISTENER(xsurface, view, map_request);

	g_views.prepend(view);

	if (xsurface->surface) {
		view->handle_associate(NULL);
	}
	if (mapped) {
		view->map();
	}
}

static void
handle_new_surface(struct wl_listener *listener, void *data)
{
	auto xsurface = (wlr_xwayland_surface *)data;
	wlr_xwayland_surface_ping(xsurface);

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

static struct xwayland_view *
xwayland_view_from_window_id(xcb_window_t id)
{
	for (auto &view : g_views) {
		if (view.type != LAB_XWAYLAND_VIEW) {
			continue;
		}
		auto xwayland_view = xwayland_view_from_view(&view);
		if (xwayland_view->xwayland_surface
				&& xwayland_view->xwayland_surface->window_id == id) {
			return xwayland_view;
		}
	}
	return NULL;
}

#define XCB_EVENT_RESPONSE_TYPE_MASK 0x7f
static bool
handle_x11_event(struct wlr_xwayland *wlr_xwayland, xcb_generic_event_t *event)
{
	switch (event->response_type & XCB_EVENT_RESPONSE_TYPE_MASK) {
	case XCB_PROPERTY_NOTIFY: {
		auto ev = (xcb_property_notify_event_t *)event;
		if (ev->atom == atoms[ATOM_NET_WM_ICON]) {
			struct xwayland_view *xwayland_view =
				xwayland_view_from_window_id(ev->window);
			if (xwayland_view) {
				update_icon(xwayland_view);
			} else {
				wlr_log(WLR_DEBUG, "icon property changed for unknown window");
			}
			return true;
		}
		break;
	}
	default:
		break;
	}

	return false;
}

static void
sync_atoms(void)
{
	xcb_connection_t *xcb_conn =
		wlr_xwayland_get_xwm_connection(g_server.xwayland);
	assert(xcb_conn);

	wlr_log(WLR_DEBUG, "Syncing X11 atoms");
	xcb_intern_atom_cookie_t cookies[ATOM_COUNT];

	/* First request everything and then loop over the results to reduce latency */
	for (size_t i = 0; i < ATOM_COUNT; i++) {
		cookies[i] = xcb_intern_atom(xcb_conn, 0,
			strlen(atom_names[i]), atom_names[i]);
	}

	for (size_t i = 0; i < ATOM_COUNT; i++) {
		xcb_generic_error_t *err = NULL;
		xcb_intern_atom_reply_t *reply =
			xcb_intern_atom_reply(xcb_conn, cookies[i], &err);
		if (reply) {
			atoms[i] = reply->atom;
			wlr_log(WLR_DEBUG, "Got X11 atom for %s: %u",
				atom_names[i], reply->atom);
		}
		if (err) {
			atoms[i] = XCB_ATOM_NONE;
			wlr_log(WLR_INFO, "Failed to get X11 atom for %s",
				atom_names[i]);
		}
		free(reply);
		free(err);
	}
}

static void
handle_server_ready(struct wl_listener *listener, void *data)
{
	/* Fire an Xwayland startup script if one (or many) can be found */
	session_run_script("xinitrc");
	sync_atoms();
}

static void
handle_xwm_ready(struct wl_listener *listener, void *data)
{
	wlr_xwayland_set_seat(g_server.xwayland, g_seat.seat);
	xwayland_update_workarea();
}

void
xwayland_server_init(struct wlr_compositor *compositor)
{
	g_server.xwayland = wlr_xwayland_create(g_server.wl_display, compositor,
		/* lazy */ !rc.xwayland_persistence);
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

	g_server.xwayland->user_event_handler = handle_x11_event;

	if (setenv("DISPLAY", g_server.xwayland->display_name, true) < 0) {
		wlr_log_errno(WLR_ERROR, "unable to set DISPLAY for xwayland");
	} else {
		wlr_log(WLR_DEBUG, "xwayland is running on display %s",
			g_server.xwayland->display_name);
	}

	struct wlr_xcursor *xcursor;
	xcursor = wlr_xcursor_manager_get_xcursor(g_seat.xcursor_manager,
		XCURSOR_DEFAULT, 1);
	if (xcursor) {
		struct wlr_xcursor_image *image = xcursor->images[0];
		wlr_xwayland_set_cursor(g_server.xwayland, image->buffer,
			image->width * 4, image->width, image->height,
			image->hotspot_x, image->hotspot_y);
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

	struct wlr_xcursor *xcursor =
		wlr_xcursor_manager_get_xcursor(g_seat.xcursor_manager,
			XCURSOR_DEFAULT, 1);

	if (xcursor && !g_server.xwayland->xwm) {
		/* Prevents setting the cursor on an active xwayland server */
		struct wlr_xcursor_image *image = xcursor->images[0];
		wlr_xwayland_set_cursor(g_server.xwayland, image->buffer,
			image->width * 4, image->width, image->height,
			image->hotspot_x, image->hotspot_y);
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
	struct wlr_xwayland *xwayland = g_server.xwayland;
	wl_list_remove(&g_server.xwayland_new_surface.link);
	wl_list_remove(&g_server.xwayland_server_ready.link);
	wl_list_remove(&g_server.xwayland_xwm_ready.link);

	/*
	 * Reset g_server.xwayland to NULL first to prevent callbacks (like
	 * server_global_filter) from accessing it as it is destroyed
	 */
	g_server.xwayland = NULL;
	wlr_xwayland_destroy(xwayland);
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
xwayland_adjust_usable_area(struct view *view, struct wlr_output_layout *layout,
		struct wlr_output *output, struct wlr_box *usable)
{
	assert(view);
	assert(layout);
	assert(output);
	assert(usable);

	if (view->type != LAB_XWAYLAND_VIEW) {
		return;
	}

	xcb_ewmh_wm_strut_partial_t *strut =
		xwayland_surface_from_view(view)->strut_partial;
	if (!strut) {
		return;
	}

	/* these are layout coordinates */
	struct wlr_box lb = { 0 };
	wlr_output_layout_get_box(layout, NULL, &lb);
	struct wlr_box ob = { 0 };
	wlr_output_layout_get_box(layout, output, &ob);

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
	wlr_output_layout_output_coords(layout, output,
		&strut_left, &strut_top);
	wlr_output_layout_output_coords(layout, output,
		&strut_right, &strut_bottom);

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
	wlr_xwayland_set_workareas(g_server.xwayland, &workarea, 1);
}
