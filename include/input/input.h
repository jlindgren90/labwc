/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_INPUT_H
#define LABWC_INPUT_H

#include <wayland-server-core.h>

struct input {
	struct wlr_input_device *wlr_input_device;
	/* Set for pointer/touch devices */
	double scroll_factor;
	struct wl_listener destroy;
	struct wl_list link; /* seat.inputs */
};

void input_handlers_init(void);
void input_handlers_finish(void);

#endif /* LABWC_INPUT_H */
