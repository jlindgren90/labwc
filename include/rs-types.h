/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_RS_TYPES_H
#define LABWC_RS_TYPES_H

typedef struct border {
	int top;
	int right;
	int bottom;
	int left;
} Border;

/* proxy types */
#ifndef WlrBox
typedef struct {
	int x, y;
	int width, height;
} WlrBox;
#endif

/* opaque types */
typedef struct view CView;
typedef struct wl_display WlDisplay;
typedef struct wl_resource WlResource;

#endif /* LABWC_RS_TYPES_H */
