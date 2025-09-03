/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_NODE_TYPE_H
#define LABWC_NODE_TYPE_H

#include "common/edge.h"

/*
 * In labwc, a "node type" indicates the role of a wlr_scene_node in the
 * overall desktop. It also maps more-or-less to the openbox concept of
 * "context" (as used when defining mouse bindings).
 *
 * Node types are defined in the order they should be processed for press
 * and hover events. Note that some of their respective interactive areas
 * overlap, so for example buttons need to come before title.
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
	LAB_NODE_TITLE,

	LAB_NODE_CORNER_TOP_LEFT,
	LAB_NODE_CORNER_TOP_RIGHT,
	LAB_NODE_CORNER_BOTTOM_RIGHT,
	LAB_NODE_CORNER_BOTTOM_LEFT,
	LAB_NODE_FRAME_TOP,
	LAB_NODE_FRAME_RIGHT,
	LAB_NODE_FRAME_BOTTOM,
	LAB_NODE_FRAME_LEFT,

	LAB_NODE_CLIENT,
	LAB_NODE_FRAME,
	LAB_NODE_ROOT,
	LAB_NODE_MENU,
	LAB_NODE_OSD,
	LAB_NODE_LAYER_SURFACE,
	LAB_NODE_LAYER_SUBSURFACE,
	LAB_NODE_UNMANAGED,
	LAB_NODE_ALL,
};

bool node_type_contains(enum lab_node_type whole, enum lab_node_type part);

enum lab_edge node_type_to_edges(enum lab_node_type type);

#endif /* LABWC_NODE_TYPE_H */
