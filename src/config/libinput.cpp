// SPDX-License-Identifier: GPL-2.0-only
#include "config/libinput.h"
#include <string.h>
#include <strings.h>
#include "common/string-helpers.h"
#include "config/rcxml.h"

enum lab_libinput_device_type
get_device_type(const char *s)
{
	if (string_null_or_empty(s)) {
		return LAB_LIBINPUT_DEVICE_NONE;
	}
	if (!strcasecmp(s, "default")) {
		return LAB_LIBINPUT_DEVICE_DEFAULT;
	}
	if (!strcasecmp(s, "touch")) {
		return LAB_LIBINPUT_DEVICE_TOUCH;
	}
	if (!strcasecmp(s, "touchpad")) {
		return LAB_LIBINPUT_DEVICE_TOUCHPAD;
	}
	if (!strcasecmp(s, "non-touch")) {
		return LAB_LIBINPUT_DEVICE_NON_TOUCH;
	}
	return LAB_LIBINPUT_DEVICE_NONE;
}

const char *
libinput_device_type_name(enum lab_libinput_device_type type)
{
	switch (type) {
	case LAB_LIBINPUT_DEVICE_NONE:
		break;
	case LAB_LIBINPUT_DEVICE_DEFAULT:
		return "default";
	case LAB_LIBINPUT_DEVICE_TOUCH:
		return "touch";
	case LAB_LIBINPUT_DEVICE_TOUCHPAD:
		return "touchpad";
	case LAB_LIBINPUT_DEVICE_NON_TOUCH:
		return "non-touch";
	}
	/* none/invalid */
	return "(none)";
}

struct libinput_category *
libinput_category_create(void)
{
	rc.libinput_categories.push_back({
		.type = LAB_LIBINPUT_DEVICE_DEFAULT,
		.pointer_speed = -2,
		.natural_scroll = -1,
		.left_handed = -1,
		.tap = LIBINPUT_CONFIG_TAP_ENABLED,
		.tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LRM,
		.tap_and_drag = -1,
		.drag_lock = -1,
		.three_finger_drag = -1,
		.accel_profile = -1,
		.middle_emu = -1,
		.dwt = -1,
		.click_method = -1,
		.scroll_method = -1,
		.send_events_mode = -1,
		.have_calibration_matrix = false,
		.scroll_factor = 1.0,
	});
	return &rc.libinput_categories.back();
}

/* After rcxml_read(), a default category always exists. */
struct libinput_category *
libinput_category_get_default(void)
{
	/*
	 * Iterate in reverse to get the last one added in case multiple
	 * 'default' profiles were created.
	 */
	for (auto iter = rc.libinput_categories.rbegin(),
			end = rc.libinput_categories.rend();
			iter != end; iter++) {
		if (iter->type == LAB_LIBINPUT_DEVICE_DEFAULT) {
			return &*iter;
		}
	}
	return NULL;
}
