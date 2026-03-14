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

// Compatible with xcb_ewmh_wm_strut_partial_t
#ifndef ViewStrutPartial
typedef struct ViewStrutPartial {
	unsigned left;
	unsigned right;
	unsigned top;
	unsigned bottom;
	unsigned left_start_y;
	unsigned left_end_y;
	unsigned right_start_y;
	unsigned right_end_y;
	unsigned top_start_x;
	unsigned top_end_x;
	unsigned bottom_start_x;
	unsigned bottom_end_x;
} ViewStrutPartial;
#endif

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
} ViewState;

typedef struct ViewScene {
	WlrSceneTree *scene_tree;
	WlrSceneTree *surface_tree; // child of scene_tree
} ViewScene;

typedef struct XSurfaceInfo {
	XSurface *xsurface;
	XId parent_xid;
	unsigned long serial;
	unsigned surface_id;
	ViewId view_id; // 0 if unmanaged
} XSurfaceInfo;

typedef struct XSurfaceProps {
	Rect geom;
	ViewSizeHints size_hints;
	_Bool position_hint;
	_Bool is_normal;
	_Bool is_dialog;
	_Bool is_modal;
	_Bool supports_delete;
	_Bool supports_take_focus;
	_Bool no_input_hint;
} XSurfaceProps;

WlrSceneTree *view_scene_tree_create(ViewId id);
void view_scene_tree_destroy(WlrSceneTree *scene_tree);
void view_scene_tree_move(WlrSceneTree *scene_tree, int x, int y);
void view_scene_tree_raise(WlrSceneTree *scene_tree);
void view_scene_tree_set_visible(WlrSceneTree *scene_tree, _Bool visible);

WlrSceneTree *view_surface_tree_create(WlrSceneTree *parent, WlrSurface *surface);

WlrSceneRect *view_fullscreen_bg_create(WlrSceneTree *scene_tree);
void view_fullscreen_bg_show_at(WlrSceneRect *fullscreen_bg, Rect rel_geom);
void view_fullscreen_bg_hide(WlrSceneRect *fullscreen_bg);

WlrSurface *xdg_toplevel_view_get_surface(CView *view);
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

WlrSurface *xwayland_view_get_surface(XSurface *xsurface);
void xwayland_view_configure(XSurface *xsurface, Rect current, Rect geom,
	_Bool *commit_move);

WlrSceneNode *xwayland_create_unmanaged_node(WlrSurface *surface);

// from cursor.h
void cursor_update_focus(void);

// from output.h
Output *output_nearest_to(int lx, int ly);
Output *output_nearest_to_cursor(void);
_Bool output_is_usable(Output *output);
Rect output_layout_coords(Output *output);
Rect output_usable_area_in_layout_coords(Output *output);
void output_update_all_usable_areas(_Bool layout_changed);

// defined in xwayland.c
void output_adjust_usable_area_for_strut_partial(Output *output,
	const ViewStrutPartial *strut);

WlrBuffer *scaled_icon_buffer_load(const char *app_id,
	CairoSurface *icon_surface);

// from labwc.h
void seat_focus_override_end(_Bool restore_focus);
void seat_focus_surface_no_notify(WlrSurface *surface);
WlrSurface *seat_get_focused_surface(void);

// from menu.h
void menu_on_view_destroy(ViewId view_id);

// from ssd.h
Border ssd_get_margin(const ViewState *view_st);

void top_layer_show_all(void);
void top_layer_hide_on_output(Output *output);

void xwayland_set_net_client_list(const XId *xids, unsigned num_xids);
void xwayland_surface_activate(XSurface *xsurface); // allows NULL xsurface
void xwayland_surface_close(XSurface *xsurface);
void xwayland_surface_configure(XSurface *surface, Rect geom);
void xwayland_surface_destroy(XSurface *xsurface);
XSurfaceProps xwayland_surface_get_props(XSurface *xsurface);
_Bool xwayland_surface_is_focused(XSurface *xsurface);
void xwayland_surface_offer_focus(XSurface *xsurface);
void xwayland_surface_publish_state(XSurface *xsurface, const ViewState *state);
void xwayland_surface_stack_above(XSurface *xsurface, XId sibling);

#endif // LABWC_VIEW_C_H
