/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_TOUCH_CONFIG_H
#define LABWC_TOUCH_CONFIG_H

#include "common/str.h"

struct touch_config_entry {
	lab_str device_name;
	lab_str output_name;
	bool force_mouse_emulation;
};

struct touch_config_entry *touch_find_config_for_device(
	const char *device_name);

#endif /* LABWC_TOUCH_CONFIG_H */
