/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_DIRECTION_H
#define LABWC_DIRECTION_H

#include "config/types.h"

enum wlr_direction direction_from_edge(enum lab_edge edge);
enum wlr_direction direction_get_opposite(enum wlr_direction direction);

#endif /* LABWC_DIRECTION_H */
