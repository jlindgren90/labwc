/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_ENUM_H
#define LABWC_ENUM_H

enum lab_button_state {
	LAB_BS_DEFAULT = 0,

	LAB_BS_HOVERD = 1 << 0,
	LAB_BS_TOGGLED = 1 << 1,
	LAB_BS_ROUNDED = 1 << 2,

	LAB_BS_ALL = LAB_BS_HOVERD | LAB_BS_TOGGLED | LAB_BS_ROUNDED,
};

/* Cursors used internally by labwc */
enum lab_cursors {
	LAB_CURSOR_CLIENT = 0,
	LAB_CURSOR_DEFAULT,
	LAB_CURSOR_GRAB,
	LAB_CURSOR_RESIZE_NW,
	LAB_CURSOR_RESIZE_N,
	LAB_CURSOR_RESIZE_NE,
	LAB_CURSOR_RESIZE_E,
	LAB_CURSOR_RESIZE_SE,
	LAB_CURSOR_RESIZE_S,
	LAB_CURSOR_RESIZE_SW,
	LAB_CURSOR_RESIZE_W,
	LAB_CURSOR_COUNT
};

/* All criteria is applied in AND logic */
enum lab_view_criteria {
	/* No filter -> all focusable views */
	LAB_VIEW_CRITERIA_NONE = 0,

	/*
	 * Includes always-on-top views, e.g.
	 * what is visible on the current workspace
	 */
	LAB_VIEW_CRITERIA_CURRENT_WORKSPACE       = 1 << 0,

	/* Positive criteria */
	LAB_VIEW_CRITERIA_FULLSCREEN              = 1 << 1,
	LAB_VIEW_CRITERIA_ALWAYS_ON_TOP           = 1 << 2,
	LAB_VIEW_CRITERIA_ROOT_TOPLEVEL           = 1 << 3,

	/* Negative criteria */
	LAB_VIEW_CRITERIA_NO_ALWAYS_ON_TOP        = 1 << 6,
	LAB_VIEW_CRITERIA_NO_SKIP_WINDOW_SWITCHER = 1 << 7,
	LAB_VIEW_CRITERIA_NO_OMNIPRESENT          = 1 << 8,
};

enum motion {
	LAB_TABLET_MOTION_ABSOLUTE = 0,
	LAB_TABLET_MOTION_RELATIVE,
};

enum resize_indicator_mode {
	LAB_RESIZE_INDICATOR_NEVER = 0,
	LAB_RESIZE_INDICATOR_ALWAYS,
	LAB_RESIZE_INDICATOR_NON_PIXEL
};

enum rotation {
	LAB_ROTATE_NONE = 0,
	LAB_ROTATE_90,
	LAB_ROTATE_180,
	LAB_ROTATE_270,
};

enum ssd_mode {
	LAB_SSD_MODE_INVALID,
	LAB_SSD_MODE_NONE,
	LAB_SSD_MODE_BORDER,
	LAB_SSD_MODE_FULL,
};

/*
 * Sequence these according to the order they should be processed for
 * press and hover events. Bear in mind that some of their respective
 * interactive areas overlap, so for example buttons need to come before title.
 */
enum ssd_part_type {
	LAB_SSD_NONE = 0,

	LAB_SSD_BUTTON_CLOSE = 1,
	LAB_SSD_BUTTON_MAXIMIZE,
	LAB_SSD_BUTTON_ICONIFY,
	LAB_SSD_BUTTON_WINDOW_ICON,
	LAB_SSD_BUTTON_WINDOW_MENU,
	LAB_SSD_BUTTON_SHADE,
	LAB_SSD_BUTTON_OMNIPRESENT,
	/* only for internal use */
	LAB_SSD_BUTTON_FIRST = LAB_SSD_BUTTON_CLOSE,
	LAB_SSD_BUTTON_LAST = LAB_SSD_BUTTON_OMNIPRESENT,
	LAB_SSD_BUTTON,

	LAB_SSD_PART_TITLEBAR,
	LAB_SSD_PART_TITLEBAR_CORNER_RIGHT,
	LAB_SSD_PART_TITLEBAR_CORNER_LEFT,
	LAB_SSD_PART_TITLE,

	/* shared by shadows, borders and extents */
	LAB_SSD_PART_CORNER_TOP_LEFT,
	LAB_SSD_PART_CORNER_TOP_RIGHT,
	LAB_SSD_PART_CORNER_BOTTOM_RIGHT,
	LAB_SSD_PART_CORNER_BOTTOM_LEFT,
	LAB_SSD_PART_TOP,
	LAB_SSD_PART_RIGHT,
	LAB_SSD_PART_BOTTOM,
	LAB_SSD_PART_LEFT,

	LAB_SSD_CLIENT,
	LAB_SSD_FRAME,
	LAB_SSD_ROOT,
	LAB_SSD_MENU,
	LAB_SSD_OSD,
	LAB_SSD_LAYER_SURFACE,
	LAB_SSD_LAYER_SUBSURFACE,
	LAB_SSD_UNMANAGED,
	LAB_SSD_ALL,
	LAB_SSD_END_MARKER
};

enum three_state {
	LAB_STATE_UNSPECIFIED = 0,
	LAB_STATE_ENABLED,
	LAB_STATE_DISABLED
};

/**
 * Directions in which a view can be maximized. "None" is used
 * internally to mean "not maximized" but is not valid in rc.xml.
 * Therefore when parsing rc.xml, "None" means "Invalid".
 */
enum view_axis {
	VIEW_AXIS_NONE = 0,
	VIEW_AXIS_HORIZONTAL = (1 << 0),
	VIEW_AXIS_VERTICAL = (1 << 1),
	VIEW_AXIS_BOTH = (VIEW_AXIS_HORIZONTAL | VIEW_AXIS_VERTICAL),
	/*
	 * If view_axis is treated as a bitfield, INVALID should never
	 * set the HORIZONTAL or VERTICAL bits.
	 */
	VIEW_AXIS_INVALID = (1 << 2),
};

enum view_edge {
	VIEW_EDGE_INVALID = 0,

	VIEW_EDGE_LEFT,
	VIEW_EDGE_RIGHT,
	VIEW_EDGE_UP,
	VIEW_EDGE_DOWN,
	VIEW_EDGE_CENTER,
};

enum view_placement_policy {
	LAB_PLACE_INVALID = 0,
	LAB_PLACE_CENTER,
	LAB_PLACE_CURSOR,
	LAB_PLACE_AUTOMATIC,
	LAB_PLACE_CASCADE,
};

#endif // LABWC_ENUM_H
