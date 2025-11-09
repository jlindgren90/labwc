/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_VIEW_C_H
#define LABWC_VIEW_C_H
/*
 * View functions implemented in C, called from Rust
 */

/* rust-friendly typedef */
typedef struct view CView;

void view_notify_app_id_change(CView *view);
void view_notify_title_change(CView *view);
void view_notify_maximized(CView *view);

void xdg_toplevel_view_map(CView *view);
void xdg_toplevel_view_unmap(CView *view, /*bool*/ int client_request);

void xdg_toplevel_view_maximize(CView *view, /*enum view_axis*/ int maximized);
void xdg_toplevel_view_set_fullscreen(CView *view, /*bool*/ int fullscreen);

void xwayland_view_map(CView *view);
void xwayland_view_unmap(CView *view, /*bool*/ int client_request);

void xwayland_view_maximize(CView *view, /*enum view_axis*/ int maximized);
void xwayland_view_minimize(CView *view, /*bool*/ int minimized);
void xwayland_view_set_fullscreen(CView *view, /*bool*/ int fullscreen);

#endif /* LABWC_VIEW_IMPL_H */
