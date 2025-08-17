// SPDX-License-Identifier: GPL-2.0-only
#include "common/direction.h"
#include <assert.h>
#include <wlr/types/wlr_output_layout.h>
#include "view.h"

enum wlr_direction
direction_from_edge(enum lab_edge edge)
{
	switch (edge) {
	case LAB_EDGE_LEFT:
		return WLR_DIRECTION_LEFT;
	case LAB_EDGE_RIGHT:
		return WLR_DIRECTION_RIGHT;
	case LAB_EDGE_UP:
		return WLR_DIRECTION_UP;
	case LAB_EDGE_DOWN:
		return WLR_DIRECTION_DOWN;
	case LAB_EDGE_CENTER:
	case LAB_EDGE_INVALID:
	default:
		return WLR_DIRECTION_UP;
	}
}

enum wlr_direction
direction_get_opposite(enum wlr_direction direction)
{
	switch (direction) {
	case WLR_DIRECTION_RIGHT:
		return WLR_DIRECTION_LEFT;
	case WLR_DIRECTION_LEFT:
		return WLR_DIRECTION_RIGHT;
	case WLR_DIRECTION_DOWN:
		return WLR_DIRECTION_UP;
	case WLR_DIRECTION_UP:
		return WLR_DIRECTION_DOWN;
	default:
		assert(0); /* Unreachable */
		return WLR_DIRECTION_UP;
	}
}
