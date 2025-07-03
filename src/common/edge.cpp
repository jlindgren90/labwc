// SPDX-License-Identifier: GPL-2.0-only
#include "common/edge.h"
#include <assert.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include <wlr/types/wlr_output_layout.h>

static_assert((int)LAB_EDGE_TOP == (int)WLR_EDGE_TOP
		&& (int)LAB_EDGE_BOTTOM == (int)WLR_EDGE_BOTTOM
		&& (int)LAB_EDGE_LEFT == (int)WLR_EDGE_LEFT
		&& (int)LAB_EDGE_RIGHT == (int)WLR_EDGE_RIGHT,
	"enum lab_edge does not match enum wlr_edges");

static_assert((int)LAB_EDGE_TOP == (int)WLR_DIRECTION_UP
		&& (int)LAB_EDGE_BOTTOM == (int)WLR_DIRECTION_DOWN
		&& (int)LAB_EDGE_LEFT == (int)WLR_DIRECTION_LEFT
		&& (int)LAB_EDGE_RIGHT == (int)WLR_DIRECTION_RIGHT,
	"enum lab_edge does not match enum wlr_direction");

enum lab_edge
lab_edge_parse(const char *direction)
{
	if (!direction) {
		return LAB_EDGE_NONE;
	}
	if (!strcasecmp(direction, "left")) {
		return LAB_EDGE_LEFT;
	} else if (!strcasecmp(direction, "up")) {
		return LAB_EDGE_TOP;
	} else if (!strcasecmp(direction, "right")) {
		return LAB_EDGE_RIGHT;
	} else if (!strcasecmp(direction, "down")) {
		return LAB_EDGE_BOTTOM;
	} else if (!strcasecmp(direction, "center")) {
		return LAB_EDGE_CENTER;
	} else {
		return LAB_EDGE_NONE;
	}
}

bool
lab_edge_is_cardinal(enum lab_edge edge)
{
	switch (edge) {
	case LAB_EDGE_TOP:
	case LAB_EDGE_BOTTOM:
	case LAB_EDGE_LEFT:
	case LAB_EDGE_RIGHT:
		return true;
	default:
		return false;
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
	case LAB_EDGE_TOP:
		return LAB_EDGE_BOTTOM;
	case LAB_EDGE_BOTTOM:
		return LAB_EDGE_TOP;
	default:
		return LAB_EDGE_NONE;
	}
}
