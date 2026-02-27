// SPDX-License-Identifier: GPL-2.0-only
//
// View-related functions implemented in C, callable from Rust
//
#ifndef LABWC_VIEW_C_H
#define LABWC_VIEW_C_H

#include "common/edge.h"
#include "rs-types.h"

// Rust likes camelcase types
#define view_axis ViewAxis
#define view_focus_mode ViewFocusMode

typedef enum view_axis {
	VIEW_AXIS_NONE = 0x0,
	VIEW_AXIS_HORIZONTAL = 0x1,
	VIEW_AXIS_VERTICAL = 0x2,
	VIEW_AXIS_BOTH = 0x3,
} ViewAxis;

// Indicates whether a view wants keyboard focus. Likely and Unlikely
// apply to XWayland views using ICCCM's "Globally Active" input model.
// The client voluntarily decides whether to take focus or not, while
// a heuristic is used to determine whether to show the view in Alt-Tab,
// taskbars, etc.
//
typedef enum view_focus_mode {
	VIEW_FOCUS_MODE_NEVER = 0,
	VIEW_FOCUS_MODE_ALWAYS,
	VIEW_FOCUS_MODE_LIKELY,
	VIEW_FOCUS_MODE_UNLIKELY,
} ViewFocusMode;

// Basic size hints (subset of XSizeHints from X11)
typedef struct view_size_hints {
	int min_width;
	int min_height;
	int width_inc;
	int height_inc;
	int base_width;
	int base_height;
} ViewSizeHints;

typedef struct view_surface_geom {
	Rect geom;
	_Bool keep_position;
	_Bool use_ssd;
} ViewSurfaceGeom;

typedef struct ViewState {
	const char *app_id;
	const char *title;
	_Bool mapped;
	_Bool ever_mapped;
	ViewFocusMode focus_mode;
	_Bool active;
	_Bool ssd_enabled;
	_Bool fullscreen;
	ViewAxis maximized;
	LabEdge tiled;
	_Bool minimized;
	_Bool always_on_top;
	_Bool inhibits_keybinds;
	Rect current;      // current displayed geometry
	Rect pending;      // expected geometry after any pending move/resize
	Rect natural_geom; // un-{maximized/fullscreen/tiled} geometry
	Output *output;
	// TODO: hide the following fields
	WlrSceneTree *scene_tree;
	WlrSceneTree *surface_tree; // child of scene_tree
} ViewState;

_Bool view_focus_impl(CView *view);

WlrSceneTree *view_scene_tree_create(ViewId id);
void view_scene_tree_destroy(WlrSceneTree *scene_tree);
void view_scene_tree_move(WlrSceneTree *scene_tree, int x, int y);
void view_scene_tree_raise(WlrSceneTree *scene_tree);
void view_scene_tree_set_visible(WlrSceneTree *scene_tree, _Bool visible);

WlrSceneTree *view_surface_tree_create(CView *view, WlrSceneTree *scene_tree);

WlrSceneRect *view_fullscreen_bg_create(WlrSceneTree *scene_tree);
void view_fullscreen_bg_show_at(WlrSceneRect *fullscreen_bg, Rect rel_geom);
void view_fullscreen_bg_hide(WlrSceneRect *fullscreen_bg);

ViewId xdg_toplevel_view_get_parent(CView *view);
ViewId xdg_toplevel_view_get_root_id(CView *view);
_Bool xdg_toplevel_view_is_modal_dialog(CView *view);
ViewSizeHints xdg_toplevel_view_get_size_hints(CView *view);
void xdg_toplevel_view_set_active(CView *view, _Bool active);
void xdg_toplevel_view_set_fullscreen(CView *view, _Bool fullscreen);
void xdg_toplevel_view_maximize(CView *view, ViewAxis maximized);
void xdg_toplevel_view_notify_tiled(CView *view);
void xdg_toplevel_view_configure(CView *view, Rect geom, _Bool *commit_move);
void xdg_toplevel_view_close(CView *view);

ViewId xwayland_view_get_root_id(CView *view);
_Bool xwayland_view_is_modal_dialog(CView *view);
ViewSizeHints xwayland_view_get_size_hints(CView *view);
ViewSurfaceGeom xwayland_view_get_surface_geom(CView *view);
_Bool xwayland_view_has_strut_partial(CView *view);
void xwayland_view_adjust_usable_area(CView *view, Output *output);
void xwayland_view_set_active(CView *view, _Bool active);
void xwayland_view_set_fullscreen(CView *view, _Bool fullscreen);
void xwayland_view_maximize(CView *view, ViewAxis maximized);
void xwayland_view_minimize(CView *view, _Bool minimized);
void xwayland_view_configure(CView *view, Rect geom, _Bool *commit_move);
void xwayland_view_raise(CView *view);
void xwayland_view_offer_focus(CView *view);
void xwayland_view_close(CView *view);

// from cursor.h
void cursor_update_focus(void);

// from output.h
Output *output_nearest_to(int lx, int ly);
Output *output_nearest_to_cursor(void);
_Bool output_is_usable(Output *output);
Rect output_layout_coords(Output *output);
Rect output_usable_area_in_layout_coords(Output *output);

WlrBuffer *scaled_icon_buffer_load(const char *app_id,
	CairoSurface *icon_surface);

// from labwc.h
void seat_focus_override_end(_Bool restore_focus);

// from ssd.h
Border ssd_get_margin(const ViewState *view_st);

void top_layer_show_all(void);
void top_layer_hide_on_output(Output *output);

#endif // LABWC_VIEW_C_H
