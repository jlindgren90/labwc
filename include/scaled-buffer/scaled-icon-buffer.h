/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_ICON_BUFFER_H
#define LABWC_SCALED_ICON_BUFFER_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include "common/reflist.h"
#include "common/str.h"

struct lab_data_buffer;
struct wlr_scene_tree;
struct wlr_scene_node;
struct wlr_scene_buffer;

struct scaled_icon_buffer {
	struct scaled_buffer *scaled_buffer;
	struct wlr_scene_buffer *scene_buffer;
	/* for window icon */
	struct view *view;
	lab_str view_app_id;
	lab_str view_icon_name;
	bool view_icon_prefer_client;
	reflist<lab_data_buffer> view_icon_buffers;
	struct {
		struct wl_listener new_app_id;
		struct wl_listener new_title;
		struct wl_listener set_icon;
		struct wl_listener destroy;
	} on_view;
	/* for general icon (e.g. in menus) */
	lab_str icon_name;

	int width;
	int height;
};

/*
 * Create an auto scaling icon buffer, providing a wlr_scene_buffer node for
 * display. It gets destroyed automatically when the backing scaled_buffer
 * is being destroyed which in turn happens automatically when the backing
 * wlr_scene_buffer (or one of its parents) is being destroyed.
 */
struct scaled_icon_buffer *scaled_icon_buffer_create(
	struct wlr_scene_tree *parent, int width, int height);

void scaled_icon_buffer_set_view(struct scaled_icon_buffer *self,
	struct view *view);

void scaled_icon_buffer_set_icon_name(struct scaled_icon_buffer *self,
	const char *icon_name);

#endif /* LABWC_SCALED_ICON_BUFFER_H */
