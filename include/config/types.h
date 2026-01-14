/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_CONFIG_TYPES_H
#define LABWC_CONFIG_TYPES_H

/*
 * Shared (basic) types related to user configuration.
 *
 * Please try to keep dependencies on other headers minimal,
 * since config/types.h gets included in many source files.
 *
 * For the full config struct, see config/rcxml.h.
 */

/**
 * Indicates whether tablet tool motion events should be reported using
 * absolute or relative coordinates
 */
enum lab_motion {
	LAB_MOTION_ABSOLUTE = 0,
	LAB_MOTION_RELATIVE,
};

enum lab_rotation {
	LAB_ROTATE_NONE = 0,
	LAB_ROTATE_90,
	LAB_ROTATE_180,
	LAB_ROTATE_270,
};

enum lab_tristate {
	LAB_STATE_UNSPECIFIED = 0,
	LAB_STATE_ENABLED,
	LAB_STATE_DISABLED
};

#endif /* LABWC_CONFIG_TYPES_H */
