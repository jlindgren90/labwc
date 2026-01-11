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

	int dx = *x - view->st->current.x;
	int dy = *y - view->st->current.y;
	if (view->st->maximized == VIEW_AXIS_HORIZONTAL) {
		if (abs(dx) < rc.unmaximize_threshold) {
			*x = view->st->current.x;
			return false;
		}
	} else if (view->st->maximized == VIEW_AXIS_VERTICAL) {
		if (abs(dy) < rc.unmaximize_threshold) {
			*y = view->st->current.y;
			return false;
		}
	} else {
		if (dx * dx + dy * dy < rc.unsnap_threshold * rc.unsnap_threshold) {
			*x = view->st->current.x;
			*y = view->st->current.y;
			return false;
		}
	}

	return true;
}
