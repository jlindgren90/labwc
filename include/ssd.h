/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SSD_H
#define LABWC_SSD_H

#include "common/node-type.h"

enum ssd_active_state {
	SSD_INACTIVE = 0,
	SSD_ACTIVE = 1,
};

#define FOR_EACH_ACTIVE_STATE(active) for (active = SSD_INACTIVE; active <= SSD_ACTIVE; active++)

struct wlr_cursor;

/* Forward declare arguments */
struct server;
struct ssd;
struct view;
struct wlr_scene;
struct wlr_scene_node;

typedef struct ViewState ViewState;

/*
 * Public SSD API
 *
 * For convenience in dealing with non-SSD views, this API allows NULL
 * ssd/button/node arguments and attempts to do something sensible in
 * that case (e.g. no-op/return default values).
 *
 * NULL scene/view arguments are not allowed.
 */
struct ssd *ssd_create(struct view *view, bool active);
struct border ssd_get_margin(const ViewState *view_st);
void ssd_set_active(struct ssd *ssd, bool active);
void ssd_update_title(struct ssd *ssd);
void ssd_update_icon(struct ssd *ssd);
void ssd_update_geometry(struct ssd *ssd);
void ssd_destroy(struct ssd *ssd);

void ssd_update_hovered_button(struct wlr_scene_node *node);

/* Public SSD helpers */

/*
 * Returns a part type that represents a mouse context like "Top", "Left" and
 * "TRCorner" when the cursor is on the window border or resizing handle.
 */
enum lab_node_type ssd_get_resizing_type(const struct ssd *ssd,
	struct wlr_cursor *cursor);

struct wlr_box ssd_max_extents(struct view *view);

#endif /* LABWC_SSD_H */
