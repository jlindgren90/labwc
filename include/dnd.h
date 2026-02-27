/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_DND_H
#define LABWC_DND_H

#include <wayland-server-core.h>

struct seat;

void dnd_init(void);
void dnd_icons_show(bool show);
void dnd_icons_move(double x, double y);
void dnd_finish(void);

#endif /* LABWC_DND_H */
