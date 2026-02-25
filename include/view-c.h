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
	Rect current;      // current displayed geometry
	Rect pending;      // expected geometry after any pending move/resize
	Rect natural_geom; // un-{maximized/fullscreen/tiled} geometry
	Output *output;
} ViewState;

void view_notify_app_id_change(CView *view);
void view_notify_title_change(CView *view);
void view_notify_map(CView *view);
void view_notify_unmap(CView *view);
void view_notify_active(CView *view);
void view_notify_ssd_enabled(CView *view);
void view_notify_move_resize(CView *view);

void xdg_toplevel_view_set_active(CView *view, _Bool active);
void xdg_toplevel_view_set_fullscreen(CView *view, _Bool fullscreen);
void xdg_toplevel_view_maximize(CView *view, ViewAxis maximized);
void xdg_toplevel_view_notify_tiled(CView *view);
void xdg_toplevel_view_configure(CView *view, Rect geom, Rect *pending, Rect *current);

void xwayland_view_set_active(CView *view, _Bool active);
void xwayland_view_set_fullscreen(CView *view, _Bool fullscreen);
void xwayland_view_maximize(CView *view, ViewAxis maximized);
void xwayland_view_minimize(CView *view, _Bool minimized);
void xwayland_view_configure(CView *view, Rect geom, Rect *pending, Rect *current);
void xwayland_view_offer_focus(CView *view);

// from output.h
Output *output_nearest_to(int lx, int ly);
Rect output_layout_coords(Output *output);
Rect output_usable_area_in_layout_coords(Output *output);

// from ssd.h
Border ssd_get_margin(const ViewState *view_st);

#endif // LABWC_VIEW_C_H
