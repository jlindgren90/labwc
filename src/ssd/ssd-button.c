// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "common/mem.h"
#include "config/rcxml.h"
#include "node.h"
#include "scaled-buffer/scaled-img-buffer.h"
#include "ssd-internal.h"

/* Internal API */

struct ssd_button *
attach_ssd_button(enum lab_node_type type, struct wlr_scene_tree *parent,
		struct lab_img *imgs[LAB_BS_ALL + 1],
		int x, int y, struct view *view)
{
	struct wlr_scene_tree *root = wlr_scene_tree_create(parent);
	wlr_scene_node_set_position(&root->node, x, y);

	assert(node_type_contains(LAB_NODE_BUTTON, type));
	struct ssd_button *button = znew(*button);
	button->node = &root->node;
	button->type = type;
	node_descriptor_create(&root->node, type, view->id, button);

	/* Hitbox */
	float invisible[4] = { 0, 0, 0, 0 };
	wlr_scene_rect_create(root, g_theme.window_button_width,
		g_theme.window_button_height, invisible);

	/* Icons */
	int button_width = g_theme.window_button_width;
	int button_height = g_theme.window_button_height;

	if (type == LAB_NODE_BUTTON_WINDOW_ICON) {
		struct wlr_scene_buffer *icon_buffer =
			wlr_scene_buffer_create(root, NULL);
		int icon_size = g_theme.window_icon_size;
		wlr_scene_buffer_set_dest_size(icon_buffer,
			icon_size, icon_size);
		wlr_scene_node_set_position(&icon_buffer->node,
			(button_width - icon_size) / 2,
			(button_height - icon_size) / 2);
		button->window_icon = icon_buffer;
	} else {
		for (uint8_t state_set = LAB_BS_DEFAULT;
				state_set <= LAB_BS_ALL; state_set++) {
			if (!imgs[state_set]) {
				continue;
			}
			struct scaled_img_buffer *img_buffer = scaled_img_buffer_create(
				root, imgs[state_set], g_theme.window_button_width,
				g_theme.window_button_height);
			assert(img_buffer);
			struct wlr_scene_node *icon_node = &img_buffer->scene_buffer->node;
			wlr_scene_node_set_enabled(icon_node, false);
			button->img_buffers[state_set] = img_buffer;
		}
		/* Initially show non-hover, non-toggled, unrounded variant */
		wlr_scene_node_set_enabled(
			&button->img_buffers[LAB_BS_DEFAULT]->scene_buffer->node, true);
	}

	return button;
}
