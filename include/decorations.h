/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_DECORATIONS_H
#define LABWC_DECORATIONS_H

struct wl_display;

void kde_deco_manager_init(struct wl_display *display);
void kde_deco_manager_finish(void);

void xdg_deco_manager_init(struct wl_display *display);
void xdg_deco_manager_finish(void);

#endif /* LABWC_DECORATIONS_H */
