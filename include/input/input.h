/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_INPUT_H
#define LABWC_INPUT_H

#include <wayland-server-core.h>
#include "common/listener.h"
#include "common/refptr.h"

struct input : public destroyable, public ref_guarded<input> {
	struct wlr_input_device *wlr_input_device;
	/* Set for pointer/touch devices */
	double scroll_factor;

	virtual ~input();
};

void input_handlers_init(void);
void input_handlers_finish(void);

#endif /* LABWC_INPUT_H */
