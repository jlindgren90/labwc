/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_VIEW_H
#define LABWC_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>
#include <wlr/util/box.h>
#include <xkbcommon/xkbcommon.h>
#include "common/edge.h"
#include "common/listener.h"
#include "common/reflist.h"
#include "common/str.h"
#include "config.h"
#include "config/types.h"

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

/*
 * In labwc, a view is a container for surfaces which can be moved around by
 * the user. In practice this means XDG toplevel and XWayland windows.
 */

enum view_type {
	LAB_XDG_SHELL_VIEW,
#if HAVE_XWAYLAND
	LAB_XWAYLAND_VIEW,
#endif
};

enum ssd_preference {
	LAB_SSD_PREF_UNSPEC = 0,
	LAB_SSD_PREF_CLIENT,
	LAB_SSD_PREF_SERVER,
};

/**
 * Directions in which a view can be maximized. "None" is used
 * internally to mean "not maximized" but is not valid in rc.xml.
 * Therefore when parsing rc.xml, "None" means "Invalid".
 */
enum view_axis {
	VIEW_AXIS_NONE = 0,
	VIEW_AXIS_HORIZONTAL = (1 << 0),
	VIEW_AXIS_VERTICAL = (1 << 1),
	VIEW_AXIS_BOTH = (VIEW_AXIS_HORIZONTAL | VIEW_AXIS_VERTICAL),
	/*
	 * If view_axis is treated as a bitfield, INVALID should never
	 * set the HORIZONTAL or VERTICAL bits.
	 */
	VIEW_AXIS_INVALID = (1 << 2),
};

enum view_wants_focus {
	/* View does not want focus */
	VIEW_WANTS_FOCUS_NEVER = 0,
	/* View wants focus */
	VIEW_WANTS_FOCUS_ALWAYS,
	/*
	 * The following values apply only to XWayland views using the
	 * Globally Active input model per the ICCCM. These views are
	 * offered focus and will voluntarily accept or decline it.
	 *
	 * In some cases, labwc needs to decide in advance whether to
	 * focus the view. For this purpose, these views are classified
	 * (by a heuristic) as likely or unlikely to want focus. However,
	 * it is still ultimately up to the client whether the view gets
	 * focus or not.
	 */
	VIEW_WANTS_FOCUS_LIKELY,
	VIEW_WANTS_FOCUS_UNLIKELY,
};

struct foreign_toplevel;
struct lab_data_buffer;
struct region;
struct view;
struct wlr_surface;
struct wlr_xdg_surface;
struct workspace;

using view_list = reflist<view>;
using view_iter = reflist<view>::iter;

/* Basic size hints (subset of XSizeHints from X11) */
struct view_size_hints {
	int min_width;
	int min_height;
	int width_inc;
	int height_inc;
	int base_width;
	int base_height;
};

struct resize_indicator {
	int width, height;
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *border;
	struct wlr_scene_rect *background;
	struct scaled_font_buffer *text;
};

struct resize_outlines {
	struct wlr_box view_geo;
	struct lab_scene_rect *rect;
};

/* C++ aggregate type holding view-related data (value-initialized) */
struct view_data {
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

	/*
	 * The outputs that the view is displayed on.
	 * This is used to notify the foreign toplevel
	 * implementation and to update the SSD invisible
	 * resize area.
	 * It is a bitset of output->scene_output->index.
	 */
	uint64_t outputs;

	struct workspace *workspace;
	struct wlr_surface *surface;
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *content_tree;

	bool mapped;
	bool been_mapped;
	bool ssd_enabled;
	bool ssd_titlebar_hidden;
	enum ssd_preference ssd_preference;
	bool shaded;
	bool minimized;
	enum view_axis maximized;
	bool fullscreen;
	bool tearing_hint;
	enum lab_tristate force_tearing;
	bool visible_on_all_workspaces;
	enum lab_edge tiled;
	enum lab_edge edges_visible;
	bool inhibits_keybinds;
	xkb_layout_index_t keyboard_layout;

	/* Pointer to an output owned struct region, may be NULL */
	weakptr<region> tiled_region;
	/* Set to region->name when tiled_region is free'd by a destroying output */
	lab_str tiled_region_evacuate;

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
	 * Whenever an output layout change triggers a view relocation, the
	 * last pending position (or natural geometry) will be saved so the
	 * view may be restored to its original location on a subsequent layout
	 * change.
	 */
	struct wlr_box last_layout_geometry;

	/* used by xdg-shell views */
	uint32_t pending_configure_serial;
	struct wl_event_source *pending_configure_timeout;

	struct ssd *ssd;
	struct resize_indicator resize_indicator;
	struct resize_outlines resize_outlines;

	struct foreign_toplevel *foreign_toplevel;

	/* used by scaled_icon_buffer */
	struct {
		lab_str name;
		reflist<lab_data_buffer> buffers;
	} icon;

	struct {
		struct wl_signal new_app_id;
		struct wl_signal new_title;
		struct wl_signal new_outputs;
		struct wl_signal maximized;
		struct wl_signal minimized;
		struct wl_signal fullscreened;
		struct wl_signal activated;     /* bool *activated */
		/*
		 * This is emitted when app_id, or icon set via xdg_toplevel_icon
		 * is updated. This is listened by scaled_icon_buffer.
		 */
		struct wl_signal set_icon;
		struct wl_signal destroy;
	} events;
};

/* C++ class representing a view (constructor-initialized) */
struct view : public destroyable, public ref_guarded<view>, public view_data {
	const view_type type;

	view(view_type type);
	virtual ~view();

	virtual void map() = 0;
	// client_request is true if the client unmapped its own
	// surface; false if we are just minimizing the view. The two
	// cases are similar but have subtle differences (e.g., when
	// minimizing we don't destroy the foreign toplevel handle).
	virtual void unmap(bool client_request) = 0;
	virtual void configure(wlr_box geo) = 0;
	virtual void close() = 0;
	virtual const char *get_string_prop(const char *prop) = 0;
	virtual void set_activated(bool activated) = 0;
	virtual void set_fullscreen(bool fullscreen) = 0;
	virtual void notify_tiled() { /* no-op */ }
	virtual void maximize(view_axis maximized) = 0;
	virtual void minimize(bool minimize) = 0;
	virtual view *get_root() = 0;
	virtual view_list get_children() = 0;
	virtual bool is_modal_dialog() { return false; }
	virtual view_size_hints get_size_hints() = 0;
	virtual view_wants_focus wants_focus() { return VIEW_WANTS_FOCUS_ALWAYS; }
	virtual void offer_focus() = 0;
	// returns true if view reserves space at screen edge
	virtual bool has_strut_partial() { return false; }
	// returns true if view declared itself a window type
	virtual bool contains_window_type(lab_window_type window_type) = 0;
	// returns the client pid that this view belongs to
	virtual pid_t get_pid() = 0;

	void handle_map(void *);
	void handle_unmap(void *) {
		unmap(/* client_request */ true);
	}

	virtual void handle_commit(void *) = 0;
	virtual void handle_request_move(void *) = 0;
	virtual void handle_request_resize(void *) = 0;
	virtual void handle_request_minimize(void *) = 0;
	virtual void handle_request_maximize(void *) = 0;
	virtual void handle_request_fullscreen(void *) = 0;
	virtual void handle_set_title(void *) = 0;

	DECLARE_LISTENER(view, map);
	DECLARE_LISTENER(view, unmap);
	DECLARE_LISTENER(view, commit);
	DECLARE_LISTENER(view, request_move);
	DECLARE_LISTENER(view, request_resize);
	DECLARE_LISTENER(view, request_minimize);
	DECLARE_LISTENER(view, request_maximize);
	DECLARE_LISTENER(view, request_fullscreen);
	DECLARE_LISTENER(view, set_title);
};

struct view_query {
	lab_str identifier;
	lab_str title;
	enum lab_window_type window_type;
	lab_str sandbox_engine;
	lab_str sandbox_app_id;
	enum lab_tristate shaded;
	enum view_axis maximized;
	enum lab_tristate iconified;
	enum lab_tristate focused;
	enum lab_tristate omnipresent;
	enum lab_edge tiled;
	lab_str tiled_region;
	lab_str desktop;
	enum lab_ssd_mode decoration;
	lab_str monitor;

	static view_query create() {
		return {
			.window_type = LAB_WINDOW_TYPE_INVALID,
			.maximized = VIEW_AXIS_INVALID
		};
	}
};

struct xdg_toplevel_view : public view {
	wlr_xdg_surface *const xdg_surface;

	xdg_toplevel_view(wlr_xdg_surface *xdg_surface)
		: view(LAB_XDG_SHELL_VIEW), xdg_surface(xdg_surface) {}
	~xdg_toplevel_view();

	void map() override;
	void unmap(bool client_request) override;
	void configure(wlr_box geo) override;
	void close() override;
	const char *get_string_prop(const char *prop) override;
	void set_activated(bool activated) override;
	void set_fullscreen(bool fullscreen) override;
	void notify_tiled() override;
	void maximize(view_axis maximized) override;
	void minimize(bool minimize) override;
	view *get_root() override;
	view_list get_children() override;
	view_size_hints get_size_hints() override;
	void offer_focus() override {}
	bool contains_window_type(lab_window_type window_type) override;
	pid_t get_pid() override;

	void handle_commit(void *) override;
	void handle_request_move(void *) override;
	void handle_request_resize(void *) override;
	void handle_request_minimize(void *) override;
	void handle_request_maximize(void *) override;
	void handle_request_fullscreen(void *) override;
	void handle_set_title(void *) override;

	/* Events unique to xdg-toplevel views */
	DECLARE_HANDLER(xdg_toplevel_view, set_app_id);
	DECLARE_HANDLER(xdg_toplevel_view, request_show_window_menu);
	DECLARE_HANDLER(xdg_toplevel_view, new_popup);
};

/* Global list of views */
extern reflist<view> g_views;

/**
 * view_from_wlr_surface() - returns the view associated with a
 * wlr_surface, or NULL if the surface has no associated view.
 */
struct view *view_from_wlr_surface(struct wlr_surface *surface);

/**
 * view_matches_query() - Check if view matches the given criteria
 * @view: View to checked.
 * @query: Criteria to match against.
 *
 * Returns true if %view matches all of the criteria given in %query, false
 * otherwise.
 */
bool view_matches_query(struct view *view, struct view_query *query);

/**
 * for_each_view() - iterate over all views which match criteria
 * @v: Name of view iterator.
 * @start: Start of range to iterate over.
 * @criteria: Criteria to match against.
 * Example:
 *	for_each_view(view, g_views, LAB_VIEW_CRITERIA_NONE) {
 *		printf("%s\n", view_get_string_prop(view.get(), "app_id"));
 *	}
 */
#define for_each_view(v, start, criteria) \
	for (auto v = (start); (v = view_find_matching(v, (criteria))); ++v)

/**
 * view_find_matching() - find first view that matches criteria
 * @start: Start of range to iterate over.
 * @criteria: Criteria to match against.
 *
 * Returns @stop if there are no views matching the criteria.
 */
view_iter view_find_matching(view_iter start, lab_view_criteria criteria);

/**
 * view_list_matching() - create list of views that match criteria
 * @server: server context
 * @criteria: criteria to match against
 *
 * This function is useful in cases where the calling function may change the
 * stacking order or where it needs to iterate over the views multiple times,
 * for example to get the number of views before processing them.
 *
 * Note: This list has a very short shelf-life so it is intended to be used
 *       with a single-use-throw-away approach.
 */
view_list view_list_matching(lab_view_criteria criteria);

enum view_wants_focus view_wants_focus(struct view *view);
bool view_contains_window_type(struct view *view, enum lab_window_type window_type);

/**
 * view_is_focusable() - Check whether or not a view can be focused
 * @view: view to be checked
 *
 * The purpose of this test is to filter out views (generally Xwayland) which
 * are not meant to be focused such as those with surfaces
 *	a. that have been created but never mapped;
 *	b. set to NULL after client minimize-request.
 *
 * The only views that are allowed to be focused are those that have a surface
 * and have been mapped at some point since creation.
 */
bool view_is_focusable(struct view *view);

/*
 * For use by desktop_focus_view() only - please do not call directly.
 * See the description of VIEW_WANTS_FOCUS_OFFER for more information.
 */
void view_offer_focus(struct view *view);

void view_toggle_keybinds(struct view *view);

void view_set_activated(struct view *view, bool activated);
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
void view_resize_relative(struct view *view,
	int left, int right, int top, int bottom);
void view_move_relative(struct view *view, int x, int y);
void view_move(struct view *view, int x, int y);
void view_move_to_cursor(struct view *view);
void view_moved(struct view *view);
void view_minimize(struct view *view, bool minimized);
bool view_compute_centered_position(struct view *view,
	const struct wlr_box *ref, int w, int h, int *x, int *y);
void view_set_fallback_natural_geometry(struct view *view);
void view_store_natural_geometry(struct view *view);

/**
 * view_apply_natural_geometry - adjust view->natural_geometry if it doesn't
 * intersect with view->output and then apply it
 */
void view_apply_natural_geometry(struct view *view);

/**
 * view_effective_height - effective height of view, with respect to shaded state
 * @view: view for which effective height is desired
 * @use_pending: if false, report current height; otherwise, report pending height
 */
int view_effective_height(struct view *view, bool use_pending);

/**
 * view_center - center view within some region
 * @view: view to be centered
 * @ref: optional reference region (in layout coordinates) to center
 * within; if NULL, view is centered within usable area of its output
 */
void view_center(struct view *view, const struct wlr_box *ref);

/**
 * view_place_by_policy - apply placement strategy to view
 * @view: view to be placed
 * @allow_cursor: set to false to ignore center-on-cursor policy
 * @policy: placement policy to apply
 */
void view_place_by_policy(struct view *view, bool allow_cursor,
	enum lab_placement_policy policy);
void view_constrain_size_to_that_of_usable_area(struct view *view);

void view_restore_to(struct view *view, struct wlr_box geometry);
void view_set_untiled(struct view *view);
void view_maximize(struct view *view, enum view_axis axis,
	bool store_natural_geometry);
void view_set_fullscreen(struct view *view, bool fullscreen);
void view_toggle_maximize(struct view *view, enum view_axis axis);
bool view_wants_decorations(struct view *view);
void view_toggle_decorations(struct view *view);

bool view_is_always_on_top(struct view *view);
bool view_is_always_on_bottom(struct view *view);
bool view_is_omnipresent(struct view *view);
void view_toggle_always_on_top(struct view *view);
void view_toggle_always_on_bottom(struct view *view);
void view_toggle_visible_on_all_workspaces(struct view *view);

bool view_is_tiled(struct view *view);
bool view_is_tiled_and_notify_tiled(struct view *view);
bool view_is_floating(struct view *view);
void view_move_to_workspace(struct view *view, struct workspace *workspace);
enum lab_ssd_mode view_get_ssd_mode(struct view *view);
void view_set_ssd_mode(struct view *view, enum lab_ssd_mode mode);
void view_set_decorations(struct view *view, enum lab_ssd_mode mode, bool force_ssd);
void view_toggle_fullscreen(struct view *view);
void view_invalidate_last_layout_geometry(struct view *view);
void view_adjust_for_layout_change(struct view *view);
void view_move_to_edge(struct view *view, enum lab_edge direction, bool snap_to_windows);
void view_grow_to_edge(struct view *view, enum lab_edge direction);
void view_shrink_to_edge(struct view *view, enum lab_edge direction);
void view_snap_to_edge(struct view *view, enum lab_edge direction,
	bool across_outputs, bool store_natural_geometry);
void view_snap_to_region(struct view *view, struct region *region, bool store_natural_geometry);
void view_move_to_output(struct view *view, struct output *output);

void view_move_to_front(struct view *view);
void view_move_to_back(struct view *view);
struct view *view_get_root(struct view *view);
view_list view_get_children(struct view *view);

/**
 * view_get_modal_dialog() - returns any modal dialog found among this
 * view's children or siblings (or possibly this view itself). Applies
 * only to xwayland views and always returns NULL for xdg-shell views.
 */
struct view *view_get_modal_dialog(struct view *view);

bool view_on_output(struct view *view, struct output *output);

/**
 * view_has_strut_partial() - returns true for views that reserve space
 * at a screen edge (e.g. panels). These views are treated as if they
 * have the fixedPosition window rule: i.e. they are not restricted to
 * the usable area and cannot be moved/resized interactively.
 */
bool view_has_strut_partial(struct view *view);

const char *view_get_string_prop(struct view *view, const char *prop);
void view_update_title(struct view *view);
void view_update_app_id(struct view *view);
void view_reload_ssd(struct view *view);
int view_get_min_width(void);

void view_set_shade(struct view *view, bool shaded);

/* Icon buffers set with this function are dropped later */
void view_set_icon(struct view *view, const char *icon_name,
	reflist<lab_data_buffer> &&buffers);

struct view_size_hints view_get_size_hints(struct view *view);
void view_adjust_size(struct view *view, int *w, int *h);

void view_evacuate_region(struct view *view);
void view_on_output_destroy(struct view *view);
void view_connect_map(struct view *view, struct wlr_surface *surface);

enum view_axis view_axis_parse(const char *direction);
enum lab_placement_policy view_placement_parse(const char *policy);

/* xdg.c */
struct wlr_xdg_surface *xdg_surface_from_view(struct view *view);

#endif /* LABWC_VIEW_H */
