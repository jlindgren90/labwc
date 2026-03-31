// SPDX-License-Identifier: GPL-2.0-only
#include "resistance.h"
#include <stdlib.h>
#include "view.h"

#define SINGLE_AXIS_UNMAXIMIZE_THRESHOLD 100
#define UNSNAP_THRESHOLD 20

bool
resistance_unsnap_apply(struct view *view, int *x, int *y)
{
	if (view_is_floating(view->st)) {
		return false;
	}

	int dx = *x - view->current.x;
	int dy = *y - view->current.y;
	if (view->st->maximized == VIEW_AXIS_HORIZONTAL) {
		if (abs(dx) < SINGLE_AXIS_UNMAXIMIZE_THRESHOLD) {
			*x = view->current.x;
			return false;
		}
	} else if (view->st->maximized == VIEW_AXIS_VERTICAL) {
		if (abs(dy) < SINGLE_AXIS_UNMAXIMIZE_THRESHOLD) {
			*y = view->current.y;
			return false;
		}
	} else {
		if (dx * dx + dy * dy < UNSNAP_THRESHOLD * UNSNAP_THRESHOLD) {
			*x = view->current.x;
			*y = view->current.y;
			return false;
		}
	}

	return true;
}
