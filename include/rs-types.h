// SPDX-License-Identifier: GPL-2.0-only
#ifndef LABWC_RS_TYPES_H
#define LABWC_RS_TYPES_H

// Compatible with wlr_box
#ifndef Rect
typedef struct {
	int x, y;
	int width, height;
} Rect;
#endif

// Unique (never re-used) ID for each view. 0 means none/invalid.
typedef unsigned long ViewId;

// Other Rust-friendly typedefs
typedef struct view CView;
typedef struct wl_display WlDisplay;
typedef struct wl_resource WlResource;

#endif // LABWC_RS_TYPES_H
