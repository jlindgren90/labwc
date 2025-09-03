// SPDX-License-Identifier: GPL-2.0-only
#include "common/node-type.h"

bool
node_type_contains(enum lab_node_type whole, enum lab_node_type part)
{
	if (whole == part || whole == LAB_NODE_ALL) {
		return true;
	}
	if (whole == LAB_NODE_BUTTON) {
		return part >= LAB_NODE_BUTTON_FIRST
			&& part <= LAB_NODE_BUTTON_LAST;
	}
	if (whole == LAB_NODE_TITLEBAR) {
		return part >= LAB_NODE_BUTTON_FIRST
			&& part <= LAB_NODE_TITLE;
	}
	if (whole == LAB_NODE_TITLE) {
		/* "Title" includes blank areas of "Titlebar" as well */
		return part >= LAB_NODE_TITLEBAR
			&& part <= LAB_NODE_TITLE;
	}
	if (whole == LAB_NODE_FRAME) {
		return part >= LAB_NODE_BUTTON_FIRST
			&& part <= LAB_NODE_CLIENT;
	}
	if (whole == LAB_NODE_FRAME_TOP) {
		return part == LAB_NODE_CORNER_TOP_LEFT
			|| part == LAB_NODE_CORNER_TOP_RIGHT;
	}
	if (whole == LAB_NODE_FRAME_RIGHT) {
		return part == LAB_NODE_CORNER_TOP_RIGHT
			|| part == LAB_NODE_CORNER_BOTTOM_RIGHT;
	}
	if (whole == LAB_NODE_FRAME_BOTTOM) {
		return part == LAB_NODE_CORNER_BOTTOM_RIGHT
			|| part == LAB_NODE_CORNER_BOTTOM_LEFT;
	}
	if (whole == LAB_NODE_FRAME_LEFT) {
		return part == LAB_NODE_CORNER_TOP_LEFT
			|| part == LAB_NODE_CORNER_BOTTOM_LEFT;
	}
	return false;
}

enum lab_edge
node_type_to_edges(enum lab_node_type type)
{
	switch (type) {
	case LAB_NODE_FRAME_TOP:
		return LAB_EDGE_TOP;
	case LAB_NODE_FRAME_RIGHT:
		return LAB_EDGE_RIGHT;
	case LAB_NODE_FRAME_BOTTOM:
		return LAB_EDGE_BOTTOM;
	case LAB_NODE_FRAME_LEFT:
		return LAB_EDGE_LEFT;
	case LAB_NODE_CORNER_TOP_LEFT:
		return LAB_EDGES_TOP_LEFT;
	case LAB_NODE_CORNER_TOP_RIGHT:
		return LAB_EDGES_TOP_RIGHT;
	case LAB_NODE_CORNER_BOTTOM_RIGHT:
		return LAB_EDGES_BOTTOM_RIGHT;
	case LAB_NODE_CORNER_BOTTOM_LEFT:
		return LAB_EDGES_BOTTOM_LEFT;
	default:
		return LAB_EDGE_NONE;
	}
}
