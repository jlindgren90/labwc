// SPDX-License-Identifier: GPL-2.0-only
#include "resistance.h"
#include "config/rcxml.h"
#include "view.h"

bool
resistance_unsnap_apply(struct view *view, int *x, int *y)
{
	if (view_is_floating(view)) {
		return false;
	}

	int dx = *x - view->current.x;
	int dy = *y - view->current.y;
	if (view->maximized == VIEW_AXIS_HORIZONTAL) {
		if (abs(dx) < rc.unmaximize_threshold) {
			*x = view->current.x;
			return false;
		}
	} else if (view->maximized == VIEW_AXIS_VERTICAL) {
		if (abs(dy) < rc.unmaximize_threshold) {
			*y = view->current.y;
			return false;
		}
	} else {
		if (dx * dx + dy * dy < rc.unsnap_threshold * rc.unsnap_threshold) {
			*x = view->current.x;
			*y = view->current.y;
			return false;
		}
	}

	return true;
}
