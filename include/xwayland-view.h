/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_XWAYLAND_VIEW_H
#define LABWC_XWAYLAND_VIEW_H

struct wlr_compositor;

void xwayland_server_init(struct wlr_compositor *compositor);
void xwayland_server_finish(void);

void xwayland_update_workarea(void);

void xwayland_reset_cursor(void);

#endif /* LABWC_XWAYLAND_VIEW_H */
