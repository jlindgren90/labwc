// SPDX-License-Identifier: GPL-2.0-only
//
#ifndef LABWC_FOREIGN_TOPLEVEL_H
#define LABWC_FOREIGN_TOPLEVEL_H

#include "rs-types.h"

typedef struct {
	_Bool maximized;
	_Bool minimized;
	_Bool activated;
	_Bool fullscreen;
} ForeignToplevelState;

void foreign_toplevel_manager_init(WlDisplay *display);
void foreign_toplevel_manager_finish(void);

WlResource *foreign_toplevel_create(WlResource *client_resource, CView *view);
void foreign_toplevel_send_app_id(WlResource *resource, const char *app_id);
void foreign_toplevel_send_title(WlResource *resource, const char *title);
void foreign_toplevel_send_state(WlResource *resource, ForeignToplevelState state);
void foreign_toplevel_send_done(WlResource *resource);
void foreign_toplevel_close(WlResource *resource);

#endif // LABWC_FOREIGN_TOPLEVEL_H
