/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SSD_H
#define LABWC_SSD_H

#include "common/node-type.h"
#include "rs-types.h"

// Rust likes camelcase types
#define ssd_active_state SsdActiveState

enum ssd_active_state {
	SSD_INACTIVE = 0,
	SSD_ACTIVE = 1,
};

#define FOR_EACH_ACTIVE_STATE(active) for (active = SSD_INACTIVE; active <= SSD_ACTIVE; active++)

// Rust-friendly typedefs
typedef struct ssd CSsd;
typedef struct ssd_button SsdButton;

/*
 * Public SSD API
 *
 * For convenience in dealing with non-SSD views, this API allows NULL
 * ssd/button/node arguments and attempts to do something sensible in
 * that case (e.g. no-op/return default values).
 *
 * NULL scene/view arguments are not allowed.
 */
CSsd *ssd_create(CView *view, WlrBuffer *icon_buffer);
Border ssd_get_margin(CView *view);
int ssd_get_corner_width(void);
void ssd_set_active(CSsd *ssd, _Bool active);
void ssd_update_title(CSsd *ssd);
int ssd_get_icon_buffer_size(void);
void ssd_update_icon(CSsd *ssd, WlrBuffer *icon_buffer);
void ssd_update_geometry(CSsd *ssd);
void ssd_destroy(CSsd *ssd);

void ssd_enable_keybind_inhibit_indicator(CSsd *ssd, _Bool enable);

void ssd_update_hovered_button(WlrSceneNode *node);

void ssd_button_free(SsdButton *button);

/* Public SSD helpers */

/*
 * Returns a part type that represents a mouse context like "Top", "Left" and
 * "TRCorner" when the cursor is on the window border or resizing handle.
 */
enum lab_node_type ssd_get_resizing_type(CView *view, WlrCursor *cursor);

struct wlr_box ssd_max_extents(CView *view);

#endif /* LABWC_SSD_H */
