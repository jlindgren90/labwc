/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_FOREIGN_TOPLEVEL_H
#define LABWC_FOREIGN_TOPLEVEL_H

struct server;
struct view;

void foreign_toplevel_enable(struct view *view);
void foreign_toplevel_disable(struct view *view);

void foreign_toplevel_update_app_id(struct view *view);
void foreign_toplevel_update_title(struct view *view);

/*
 * To be called if any of the following changes:
 *   - maximized
 *   - minimized
 *   - activated
 *   - fullscreen
 */
void foreign_toplevel_update_state(struct view *view);

void foreign_toplevel_manager_init(struct server *server);
void foreign_toplevel_manager_finish(struct server *server);

#endif /* LABWC_FOREIGN_TOPLEVEL_H */
