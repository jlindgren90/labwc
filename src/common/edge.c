// SPDX-License-Identifier: GPL-2.0-only
#include "common/edge.h"
#include <strings.h>

enum lab_edge
lab_edge_parse(const char *direction)
{
	if (!direction) {
		return LAB_EDGE_INVALID;
	}
	if (!strcasecmp(direction, "left")) {
		return LAB_EDGE_LEFT;
	} else if (!strcasecmp(direction, "up")) {
		return LAB_EDGE_UP;
	} else if (!strcasecmp(direction, "right")) {
		return LAB_EDGE_RIGHT;
	} else if (!strcasecmp(direction, "down")) {
		return LAB_EDGE_DOWN;
	} else if (!strcasecmp(direction, "center")) {
		return LAB_EDGE_CENTER;
	} else {
		return LAB_EDGE_INVALID;
	}
}

enum lab_edge
lab_edge_invert(enum lab_edge edge)
{
	switch (edge) {
	case LAB_EDGE_LEFT:
		return LAB_EDGE_RIGHT;
	case LAB_EDGE_RIGHT:
		return LAB_EDGE_LEFT;
	case LAB_EDGE_UP:
		return LAB_EDGE_DOWN;
	case LAB_EDGE_DOWN:
		return LAB_EDGE_UP;
	default:
		return LAB_EDGE_INVALID;
	}
}
