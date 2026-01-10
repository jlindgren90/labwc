/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_UTIL_H
#define LABWC_UTIL_H

#include <wlr/util/box.h>

/* Rust -> C proxy types */
#define WlrBox struct wlr_box
#include "rs-types.h"
#include "util-rs.h"

#endif
