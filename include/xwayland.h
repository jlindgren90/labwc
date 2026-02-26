/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_XWAYLAND_H
#define LABWC_XWAYLAND_H

#include "view.h"

struct wlr_compositor;

void xwayland_unmanaged_create(struct wlr_xwayland_surface *xsurface, bool mapped);

void xwayland_view_create(struct wlr_xwayland_surface *xsurface, bool mapped);

void xwayland_server_init(struct wlr_compositor *compositor);
void xwayland_server_finish(void);

void xwayland_update_workarea(void);

void xwayland_reset_cursor(void);

#endif /* LABWC_XWAYLAND_H */
