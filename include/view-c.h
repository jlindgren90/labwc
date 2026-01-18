// SPDX-License-Identifier: GPL-2.0-only
//
// View-related functions implemented in C, callable from Rust
//
#ifndef LABWC_VIEW_C_H
#define LABWC_VIEW_C_H

#include "rs-types.h"

void interactive_cancel(CView *view);
_Bool interactive_move_is_active(CView *view);

Border ssd_get_margin(CView *view);

_Bool view_discover_output(CView *view, const Rect *geometry);
_Bool view_has_usable_output(CView *view);
Rect view_get_output_area(CView *view);
Rect view_get_output_usable_area(CView *view);

void view_set_visible(CView *view, _Bool visible);
void view_notify_visible(CView *view);

void view_move_to_front_impl(CView *view);
void view_notify_move_to_front(CView *view);

void view_notify_app_id_change(CView *view);
void view_notify_title_change(CView *view);
void view_notify_map(CView *view);
void view_notify_unmap(CView *view);
void view_notify_move_resize(CView *view);
void view_notify_fullscreen(CView *view);

unsigned long xdg_toplevel_view_get_root_id(CView *view);
void xdg_toplevel_view_configure(CView *view, Rect geom, Rect *pending, Rect *current);
void xdg_toplevel_view_maximize(CView *view, /*enum view_axis*/ int maximized);
void xdg_toplevel_view_notify_tiled(CView *view);
void xdg_toplevel_view_set_activated(CView *view, _Bool activated);
void xdg_toplevel_view_set_fullscreen(CView *view, _Bool fullscreen);

unsigned long xwayland_view_get_root_id(CView *view);
void xwayland_view_configure(CView *view, Rect geom, Rect *pending, Rect *current);
void xwayland_view_maximize(CView *view, /*enum view_axis*/ int maximized);
void xwayland_view_minimize(CView *view, _Bool minimized);
void xwayland_view_offer_focus(CView *view);
void xwayland_view_set_activated(CView *view, _Bool activated);
void xwayland_view_set_fullscreen(CView *view, _Bool fullscreen);

#endif // LABWC_VIEW_C_H
