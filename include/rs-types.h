// SPDX-License-Identifier: GPL-2.0-only
#ifndef LABWC_RS_TYPES_H
#define LABWC_RS_TYPES_H

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
typedef struct wlr_cursor WlrCursor;
typedef struct wlr_scene_node WlrSceneNode;
typedef struct wlr_scene_rect WlrSceneRect;
typedef struct wlr_scene_tree WlrSceneTree;

#endif // LABWC_RS_TYPES_H
