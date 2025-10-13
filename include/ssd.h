/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SSD_H
#define LABWC_SSD_H

#include "common/node-type.h"
#include "common/refptr.h"
#include "config/types.h"

enum ssd_active_state {
	SSD_INACTIVE = 0,
	SSD_ACTIVE = 1,
};

#define FOR_EACH_ACTIVE_STATE(active) \
	for (active = SSD_INACTIVE; \
		active <= SSD_ACTIVE; \
		active = (enum ssd_active_state)(active + 1))

struct wlr_cursor;

/*
 * Shadows should start at a point inset from the actual window border, see
 * discussion on https://github.com/labwc/labwc/pull/1648.  This constant
 * specifies inset as a multiple of visible shadow size.
 */
#define SSD_SHADOW_INSET 0.3

/* Forward declare arguments */
struct border;
struct ssd;
struct view;
struct wlr_scene_node;

/*
 * Public SSD API
 *
 * For convenience in dealing with non-SSD views, this API allows NULL
 * ssd/button/node arguments and attempts to do something sensible in
 * that case (e.g. no-op/return default values).
 *
 * NULL scene/view arguments are not allowed.
 */
class ssd_handle
{
public:
	void create(view &view, bool active);
	void destroy() { impl.reset(); }

	explicit operator bool() const { return (bool)impl; }

	border get_margin();

	void update_margin();
	void set_active(bool active);
	void update_title();
	void update_geometry();
	void set_titlebar(bool enabled);

	void enable_keybind_inhibit_indicator(bool enable);
	void enable_shade(bool enable);

	/*
	 * Returns a part type that represents a mouse context like
	 * "Top", "Left" and "TRCorner" when the cursor is on the
	 * window border or resizing handle.
	 */
	lab_node_type get_resizing_type(wlr_cursor *cursor);

	bool debug_is_root_node(wlr_scene_node *node);
	const char *debug_get_node_name(wlr_scene_node *node);

private:
	static void destroy_impl(ssd *);
	ownptr<ssd, destroy_impl> impl;
};

int ssd_get_corner_width(void);

void ssd_update_hovered_button(struct wlr_scene_node *node);

/* Public SSD helpers */

enum lab_ssd_mode ssd_mode_parse(const char *mode);

/* TODO: clean up / update */
struct border ssd_thickness(struct view *view);
struct wlr_box ssd_max_extents(struct view *view);

#endif /* LABWC_SSD_H */
