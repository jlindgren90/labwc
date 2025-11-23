/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_FOREIGN_TOPLEVEL_H
#define LABWC_FOREIGN_TOPLEVEL_H

struct view;
struct foreign_toplevel;

struct foreign_toplevel *foreign_toplevel_create(struct view *view);

void foreign_toplevel_set_parent(struct foreign_toplevel *toplevel,
	struct foreign_toplevel *parent);

void foreign_toplevel_update_app_id(struct foreign_toplevel *toplevel);

void foreign_toplevel_update_title(struct foreign_toplevel *toplevel);

void foreign_toplevel_update_outputs(struct foreign_toplevel *toplevel);

void foreign_toplevel_update_maximized(struct foreign_toplevel *toplevel);

void foreign_toplevel_update_minimized(struct foreign_toplevel *toplevel);

void foreign_toplevel_update_fullscreen(struct foreign_toplevel *toplevel);

void foreign_toplevel_update_activated(struct foreign_toplevel *toplevel);

void foreign_toplevel_destroy(struct foreign_toplevel *toplevel);

#endif /* LABWC_FOREIGN_TOPLEVEL_H */
