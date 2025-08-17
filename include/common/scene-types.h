/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCENE_TYPES_H
#define LABWC_SCENE_TYPES_H

/*
 * Shared types that are loosely related to the scene-graph.
 *
 * Please try to keep dependencies on other headers minimal,
 * since scene-types.h gets included in many source files.
 */

enum lab_button_state {
	LAB_BS_DEFAULT = 0,

	LAB_BS_HOVERED = 1 << 0,
	LAB_BS_TOGGLED = 1 << 1,
	LAB_BS_ROUNDED = 1 << 2,

	LAB_BS_ALL = LAB_BS_HOVERED | LAB_BS_TOGGLED | LAB_BS_ROUNDED,
};

/*
 * Sequence these according to the order they should be processed for
 * press and hover events. Bear in mind that some of their respective
 * interactive areas overlap, so for example buttons need to come before
 * title.
 */
enum lab_node_type {
	LAB_NODE_NONE = 0,

	LAB_NODE_BUTTON_CLOSE = 1,
	LAB_NODE_BUTTON_MAXIMIZE,
	LAB_NODE_BUTTON_ICONIFY,
	LAB_NODE_BUTTON_WINDOW_ICON,
	LAB_NODE_BUTTON_WINDOW_MENU,
	LAB_NODE_BUTTON_SHADE,
	LAB_NODE_BUTTON_OMNIPRESENT,

	LAB_NODE_BUTTON_FIRST = LAB_NODE_BUTTON_CLOSE,
	LAB_NODE_BUTTON_LAST = LAB_NODE_BUTTON_OMNIPRESENT,

	LAB_NODE_BUTTON,

	LAB_NODE_TITLEBAR,
	LAB_NODE_TITLEBAR_CORNER_RIGHT,
	LAB_NODE_TITLEBAR_CORNER_LEFT,
	LAB_NODE_TITLE,

	/* shared by shadows, borders and extents */
	LAB_NODE_CORNER_TOP_LEFT,
	LAB_NODE_CORNER_TOP_RIGHT,
	LAB_NODE_CORNER_BOTTOM_RIGHT,
	LAB_NODE_CORNER_BOTTOM_LEFT,
	LAB_NODE_EDGE_TOP,
	LAB_NODE_EDGE_RIGHT,
	LAB_NODE_EDGE_BOTTOM,
	LAB_NODE_EDGE_LEFT,

	LAB_NODE_CLIENT,
	LAB_NODE_FRAME,
	LAB_NODE_ROOT,
	LAB_NODE_MENU,
	LAB_NODE_OSD,
	LAB_NODE_LAYER_SURFACE,
	LAB_NODE_LAYER_SUBSURFACE,
	LAB_NODE_UNMANAGED,

	LAB_NODE_ALL,

	LAB_NODE_END_MARKER
};

#endif /* LABWC_SCENE_TYPES_H */
