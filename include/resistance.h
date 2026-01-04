/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_RESISTANCE_H
#define LABWC_RESISTANCE_H

#include <stdbool.h>

struct view;

/**
 * resistance_unsnap_apply() - Apply resistance when dragging a
 * maximized/tiled window. Returns true when the view needs to be un-tiled.
 */
bool resistance_unsnap_apply(struct view *view, int *x, int *y);

#endif /* LABWC_RESISTANCE_H */
