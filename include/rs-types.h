/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_RS_TYPES_H
#define LABWC_RS_TYPES_H

/* proxy types */
#ifndef WlrBox
struct WlrBox {
	int x, y;
	int width, height;
};
#endif

/* opaque types */
typedef struct view CView;
typedef struct wl_display WlDisplay;
typedef struct wl_resource WlResource;

#endif /* LABWC_RS_TYPES_H */
