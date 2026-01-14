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

typedef enum view_axis {
	VIEW_AXIS_NONE = 0x0,
	VIEW_AXIS_HORIZONTAL = 0x1,
	VIEW_AXIS_VERTICAL = 0x2,
	VIEW_AXIS_BOTH = 0x3,
} ViewAxis;

typedef struct ViewState {
	const char *app_id;
	const char *title;
	_Bool mapped;
	_Bool ever_mapped;
	_Bool active;
	_Bool fullscreen;
	ViewAxis maximized;
	LabEdge tiled;
	_Bool minimized;
} ViewState;

void view_notify_app_id_change(CView *view);
void view_notify_title_change(CView *view);
void view_notify_map(CView *view);
void view_notify_unmap(CView *view);
void view_notify_active(CView *view);
void view_notify_maximized(CView *view);

void xdg_toplevel_view_set_active(CView *view, _Bool active);
void xdg_toplevel_view_set_fullscreen(CView *view, _Bool fullscreen);
void xdg_toplevel_view_maximize(CView *view, ViewAxis maximized);
void xdg_toplevel_view_notify_tiled(CView *view);

void xwayland_view_set_active(CView *view, _Bool active);
void xwayland_view_set_fullscreen(CView *view, _Bool fullscreen);
void xwayland_view_maximize(CView *view, ViewAxis maximized);
void xwayland_view_minimize(CView *view, _Bool minimized);

#endif // LABWC_VIEW_C_H
