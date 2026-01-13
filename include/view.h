/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_VIEW_H
#define LABWC_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>
#include <wlr/util/box.h>
#include <xkbcommon/xkbcommon.h>
#include "common/edge.h"

#include "view-c.h"
#include "view_api.h"

/*
 * Default minimal window size. Clients can explicitly set smaller values via
 * e.g. xdg_toplevel::set_min_size.
 */
#define LAB_MIN_VIEW_WIDTH 100
#define LAB_MIN_VIEW_HEIGHT 60

/*
 * Fallback view geometry used in some cases where a better position
 * and/or size can't be determined. Try to avoid using these except as
 * a last resort.
 */
#define VIEW_FALLBACK_X 100
#define VIEW_FALLBACK_Y 100
#define VIEW_FALLBACK_WIDTH  640
#define VIEW_FALLBACK_HEIGHT 480

enum view_layer {
	VIEW_LAYER_NORMAL = 0,
	VIEW_LAYER_ALWAYS_ON_TOP,
};

struct view;
struct wlr_surface;

/* Basic size hints (subset of XSizeHints from X11) */
struct view_size_hints {
	int min_width;
	int min_height;
	int width_inc;
	int height_inc;
	int base_width;
	int base_height;
};

struct view_impl {
	void (*configure)(struct view *view, struct wlr_box geo);
	void (*close)(struct view *view);
	struct view *(*get_parent)(struct view *self);
	struct view *(*get_root)(struct view *self);
	void (*append_children)(struct view *self, struct wl_array *children);
	bool (*is_modal_dialog)(struct view *self);
	struct view_size_hints (*get_size_hints)(struct view *self);
	/* returns true if view reserves space at screen edge */
	bool (*has_strut_partial)(struct view *self);
};

struct view {
	const struct view_impl *impl;
	struct wl_list link;

	/* This is cleared when the view is not in the cycle list */
	struct wl_list cycle_link;

	/* rust interop */
	ViewId id;
	const ViewState *st;

	/*
	 * The primary output that the view is displayed on. Specifically:
	 *
	 *  - For floating views, this is the output nearest to the
	 *    center of the view. It is computed automatically when the
	 *    view is moved or the output layout changes.
	 *
	 *  - For fullscreen/maximized/tiled views, this is the output
	 *    used to compute the view's geometry. The view remains on
	 *    the same output unless it is disabled or disconnected.
	 *
	 * Many view functions (e.g. view_center(), view_fullscreen(),
	 * view_maximize(), etc.) allow specifying a particular output
	 * by calling view_set_output() beforehand.
	 */
	struct output *output;

	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *content_tree; /* may be NULL for unmapped view */

	enum view_layer layer;
	bool inhibits_keybinds; /* also inhibits mousebinds */

	/*
	 * Geometry of the wlr_surface contained within the view, as
	 * currently displayed. Should be kept in sync with the
	 * scene-graph at all times.
	 */
	struct wlr_box current;
	/*
	 * Expected geometry after any pending move/resize requests
	 * have been processed. Should match current geometry when no
	 * move/resize requests are pending.
	 */
	struct wlr_box pending;
	/*
	 * Saved geometry which will be restored when the view returns
	 * to normal/floating state after being maximized/fullscreen/
	 * tiled. Values are undefined/out-of-date when the view is not
	 * maximized/fullscreen/tiled.
	 */
	struct wlr_box natural_geometry;
	/*
	 * last_placement represents the last view position set by the user.
	 * output_name and relative_geo are used to keep or restore the view
	 * position relative to the output and layout_geo is used to keep the
	 * global position when the output is lost.
	 */
	struct {
		char *output_name;
		/* view geometry in output-relative coordinates */
		struct wlr_box relative_geo;
		/* view geometry in layout coordinates */
		struct wlr_box layout_geo;
	} last_placement;
	/* Set temporarily when moving view due to layout change */
	bool adjusting_for_layout_change;

	/* used by xdg-shell views */
	uint32_t pending_configure_serial;
	struct wl_event_source *pending_configure_timeout;

	struct ssd *ssd;

	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_minimize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;

	/* used by scaled_icon_buffer */
	struct {
		struct wl_array buffers; /* struct lab_data_buffer * */
	} icon;

	struct lab_data_buffer *icon_buffer;

	/* xdg-shell view fields */
	struct wlr_xdg_surface *xdg_surface;

	/* Optional black background fill behind fullscreen view */
	struct wlr_scene_rect *fullscreen_bg;

	/* Events unique to xdg-toplevel views */
	struct wl_listener set_app_id;
	struct wl_listener request_show_window_menu;
	struct wl_listener new_popup;

	/* xwayland view fields */
	struct wlr_xwayland_surface *xwayland_surface;
	bool focused_before_map;

	/* Events unique to XWayland views */
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_above;
	struct wl_listener request_activate;
	struct wl_listener request_configure;
	struct wl_listener set_class;
	struct wl_listener set_decorations;
	struct wl_listener set_override_redirect;
	struct wl_listener set_strut_partial;
	struct wl_listener set_window_type;
	struct wl_listener set_icon;
	struct wl_listener focus_in;
	struct wl_listener map_request;
};

/**
 * view_from_wlr_surface() - returns the view associated with a
 * wlr_surface, or NULL if the surface has no associated view.
 */
struct view *view_from_wlr_surface(struct wlr_surface *surface);

struct wlr_surface *view_get_surface(struct view *view);

struct view *view_get_root(struct view *view);

struct wlr_box view_get_edge_snap_box(struct view *view, struct output *output,
	enum lab_edge edge);

void view_toggle_keybinds(struct view *view);
bool view_inhibits_actions(struct view *view, struct wl_list *actions);

void view_set_output(struct view *view, struct output *output);
void view_close(struct view *view);

/**
 * view_move_resize - resize and move view
 * @view: view to be resized and moved
 * @geo: the new geometry
 * NOTE: Only use this when the view actually changes width and/or height
 * otherwise the serials might cause a delay in moving xdg-shell clients.
 * For move only, use view_move()
 */
void view_move_resize(struct view *view, struct wlr_box geo);
void view_move(struct view *view, int x, int y);
void view_moved(struct view *view);
void view_minimize(struct view *view, bool minimized);
bool view_compute_centered_position(struct view *view,
	const struct wlr_box *ref, int w, int h, int *x, int *y);
struct wlr_box view_get_fallback_natural_geometry(struct view *view);
void view_store_natural_geometry(struct view *view);

/**
 * view_apply_natural_geometry - adjust view->natural_geometry if it doesn't
 * intersect with view->output and then apply it
 */
void view_apply_natural_geometry(struct view *view);

/**
 * view_center - center view within some region
 * @view: view to be centered
 * @ref: optional reference region (in layout coordinates) to center
 * within; if NULL, view is centered within usable area of its output
 */
void view_center(struct view *view, const struct wlr_box *ref);

void view_constrain_size_to_that_of_usable_area(struct view *view);

void view_maximize(struct view *view, enum view_axis axis);
void view_set_fullscreen(struct view *view, bool fullscreen);
void view_toggle_maximize(struct view *view, enum view_axis axis);

void view_set_layer(struct view *view, enum view_layer layer);
void view_toggle_always_on_top(struct view *view);

void view_toggle_fullscreen(struct view *view);
void view_adjust_for_layout_change(struct view *view);
void view_snap_to_edge(struct view *view, enum lab_edge direction);

void view_move_to_front(struct view *view);

bool view_is_modal_dialog(struct view *view);

/**
 * view_get_modal_dialog() - returns any modal dialog found among this
 * view's children or siblings (or possibly this view itself). Applies
 * only to xwayland views and always returns NULL for xdg-shell views.
 */
struct view *view_get_modal_dialog(struct view *view);

/**
 * view_has_strut_partial() - returns true for views that reserve space
 * at a screen edge (e.g. panels). These views are treated as if they
 * have the fixedPosition window rule: i.e. they are not restricted to
 * the usable area and cannot be moved/resized interactively.
 */
bool view_has_strut_partial(struct view *view);

void view_reload_ssd(struct view *view);

struct lab_data_buffer *view_get_icon_buffer(struct view *view);

/* Icon buffers set with this function are dropped later */
void view_set_icon(struct view *view, struct wl_array *buffers);

struct view_size_hints view_get_size_hints(struct view *view);
void view_adjust_size(struct view *view, int *w, int *h);

void view_on_output_destroy(struct view *view);
void view_update_visibility(struct view *view);

void view_init(struct view *view, bool is_xwayland);
void view_destroy(struct view *view);

enum view_axis view_axis_parse(const char *direction);

/* xdg.c */
struct wlr_xdg_surface *xdg_surface_from_view(struct view *view);

#endif /* LABWC_VIEW_H */
