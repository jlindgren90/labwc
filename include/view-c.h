// SPDX-License-Identifier: GPL-2.0-only
//
// View-related functions implemented in C, callable from Rust
//
#ifndef LABWC_VIEW_C_H
#define LABWC_VIEW_C_H

#include "rs-types.h"

void view_notify_app_id_change(CView *view);
void view_notify_title_change(CView *view);
void view_notify_map(CView *view);
void view_notify_unmap(CView *view);
void view_notify_move_resize(CView *view);

void xdg_toplevel_view_configure(CView *view, Rect geom, Rect *pending, Rect *current);
void xdg_toplevel_view_maximize(CView *view, /*enum view_axis*/ int maximized);
void xdg_toplevel_view_notify_tiled(CView *view);
void xdg_toplevel_view_set_activated(CView *view, _Bool activated);
void xdg_toplevel_view_set_fullscreen(CView *view, _Bool fullscreen);

void xwayland_view_configure(CView *view, Rect geom, Rect *pending, Rect *current);
void xwayland_view_maximize(CView *view, /*enum view_axis*/ int maximized);
void xwayland_view_minimize(CView *view, _Bool minimized);
void xwayland_view_offer_focus(CView *view);
void xwayland_view_set_activated(CView *view, _Bool activated);
void xwayland_view_set_fullscreen(CView *view, _Bool fullscreen);

#endif // LABWC_VIEW_C_H
