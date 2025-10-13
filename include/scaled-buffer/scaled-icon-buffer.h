/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_ICON_BUFFER_H
#define LABWC_SCALED_ICON_BUFFER_H

#include "common/reflist.h"
#include "common/str.h"
#include "scaled-buffer.h"

struct view;
struct wlr_scene_node;

/* Auto scaling icon buffer, providing a wlr_scene_buffer node for display */
struct scaled_icon_buffer : public scaled_buffer {
	// for window icon
	refptr<::view> view;
	lab_str view_app_id;
	lab_str view_icon_name;
	bool view_icon_prefer_client = false;
	reflist<lab_data_buffer> view_icon_buffers;
	// for general icon (e.g. in menus)
	lab_str icon_name;

	int width = 0;
	int height = 0;

	scaled_icon_buffer(wlr_scene_tree *parent, int width, int height);

	refptr<lab_data_buffer> create_buffer(double scale) override;
	bool equal(scaled_buffer &other) override;

	// view listeners
	DECLARE_HANDLER(scaled_icon_buffer, new_app_id);
	DECLARE_HANDLER(scaled_icon_buffer, new_title);
	DECLARE_HANDLER(scaled_icon_buffer, set_icon);
	DECLARE_HANDLER(scaled_icon_buffer, destroy);
};

void scaled_icon_buffer_set_view(struct scaled_icon_buffer *self,
	struct view *view);

void scaled_icon_buffer_set_icon_name(struct scaled_icon_buffer *self,
	const char *icon_name);

#endif /* LABWC_SCALED_ICON_BUFFER_H */
