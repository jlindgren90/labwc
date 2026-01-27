// SPDX-License-Identifier: GPL-2.0-only
#ifndef LABWC_RS_TYPES_H
#define LABWC_RS_TYPES_H

// Rust likes camelcase types
#define input_mode InputMode

typedef enum input_mode {
	INPUT_MODE_NORMAL = 0,
	INPUT_MODE_MOVE,
	INPUT_MODE_RESIZE,
	INPUT_MODE_MENU,
	INPUT_MODE_CYCLE,
} InputMode;

typedef struct border {
	int top;
	int right;
	int bottom;
	int left;
} Border;

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
typedef struct output Output;
typedef struct _cairo_surface CairoSurface;
typedef struct wl_display WlDisplay;
typedef struct wl_resource WlResource;
typedef struct wlr_buffer WlrBuffer;

#endif // LABWC_RS_TYPES_H
