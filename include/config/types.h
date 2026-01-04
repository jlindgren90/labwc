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

enum lab_ssd_mode {
	LAB_SSD_MODE_NONE = 0,
	LAB_SSD_MODE_BORDER,
	LAB_SSD_MODE_FULL,
	LAB_SSD_MODE_INVALID,
};

enum lab_tristate {
	LAB_STATE_UNSPECIFIED = 0,
	LAB_STATE_ENABLED,
	LAB_STATE_DISABLED
};

/*
 * This enum type is a set of bit flags where each set bit makes the
 * criteria more restrictive. For example:
 *
 * (LAB_VIEW_CRITERIA_FULLSCREEN | LAB_VIEW_CRITERIA_CURRENT_WORKSPACE)
 * matches only fullscreen views on the current workspace, while
 *
 * (LAB_VIEW_CRITERIA_ALWAYS_ON_TOP | LAB_VIEW_CRITERIA_NO_ALWAYS_ON_TOP)
 * would be contradictory and match nothing at all.
 */
enum lab_view_criteria {
	/* No filter -> all focusable views */
	LAB_VIEW_CRITERIA_NONE = 0,

	/* Positive criteria */
	LAB_VIEW_CRITERIA_FULLSCREEN              = 1 << 1,
	LAB_VIEW_CRITERIA_ALWAYS_ON_TOP           = 1 << 2,
	LAB_VIEW_CRITERIA_ROOT_TOPLEVEL           = 1 << 3,

	/* Negative criteria */
	LAB_VIEW_CRITERIA_NO_ALWAYS_ON_TOP        = 1 << 6,
};

#endif /* LABWC_CONFIG_TYPES_H */
