// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include "common/list.h"
#include "common/mem.h"
#include "common/scaled-icon-buffer.h"
#include "common/scaled-img-buffer.h"
#include "config/rcxml.h"
#include "node.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

/* Internal helpers */
static void
handle_button_node_destroy(struct wl_listener *listener, void *data)
{
	struct ssd_button *button = wl_container_of(listener, button, destroy);
	wl_list_remove(&button->destroy.link);
	free(button);
}

/*
 * Create a new node_descriptor containing a link to a new ssd_button struct.
 * Both will be destroyed automatically once the scene_node they are attached
 * to is destroyed.
 */
static struct ssd_button *
ssd_button_descriptor_create(struct wlr_scene_node *node)
{
	/* Create new ssd_button */
	struct ssd_button *button = znew(*button);

	/* Let it destroy automatically when the scene node destroys */
	button->destroy.notify = handle_button_node_destroy;
	wl_signal_add(&node->events.destroy, &button->destroy);

	/* And finally attach the ssd_button to a node descriptor */
	node_descriptor_create(node, LAB_NODE_DESC_SSD_BUTTON, button);
	return button;
}

/* Internal API */
struct ssd_part *
add_scene_part(struct wl_list *part_list, enum lab_node_type type)
{
	struct ssd_part *part = znew(*part);
	part->type = type;
	wl_list_append(part_list, &part->link);
	return part;
}

struct ssd_part *
add_scene_rect(struct wl_list *list, enum lab_node_type type,
	struct wlr_scene_tree *parent, int width, int height,
	int x, int y, float color[4])
{
	assert(width >= 0 && height >= 0);
	struct ssd_part *part = add_scene_part(list, type);
	part->node = &wlr_scene_rect_create(
		parent, width, height, color)->node;
	wlr_scene_node_set_position(part->node, x, y);
	return part;
}

struct ssd_part *
add_scene_buffer(struct wl_list *list, enum lab_node_type type,
	struct wlr_scene_tree *parent, struct wlr_buffer *buffer,
	int x, int y)
{
	struct ssd_part *part = add_scene_part(list, type);
	part->node = &wlr_scene_buffer_create(parent, buffer)->node;
	wlr_scene_node_set_position(part->node, x, y);
	return part;
}

struct ssd_part *
add_scene_button(struct wl_list *part_list, enum lab_node_type type,
		struct wlr_scene_tree *parent, lab_img imgs[LAB_BS_ALL + 1],
		int x, int y, struct view *view)
{
	struct ssd_part *button_root = add_scene_part(part_list, type);
	parent = wlr_scene_tree_create(parent);
	button_root->node = &parent->node;
	wlr_scene_node_set_position(button_root->node, x, y);

	struct ssd_button *button = ssd_button_descriptor_create(button_root->node);
	button->type = type;
	button->view = view;

	/* Hitbox */
	float invisible[4] = { 0, 0, 0, 0 };
	add_scene_rect(part_list, type, parent, g_theme.window_button_width,
		g_theme.window_button_height, 0, 0, invisible);

	/* Icons */
	int button_width = g_theme.window_button_width;
	int button_height = g_theme.window_button_height;
	/*
	 * Ensure a small amount of horizontal padding within the button
	 * area (2px on each side with the default 26px button width).
	 * A new theme setting could be added to configure this. Using
	 * an existing setting (padding.width or window.button.spacing)
	 * was considered, but these settings have distinct purposes
	 * already and are zero by default.
	 */
	int icon_padding = button_width / 10;

	if (type == LAB_NODE_BUTTON_WINDOW_ICON) {
		struct ssd_part *icon_part = add_scene_part(part_list, type);
		auto icon_buffer = new scaled_icon_buffer(parent,
			button_width - 2 * icon_padding, button_height);
		scaled_icon_buffer_set_view(icon_buffer, view);
		icon_part->node = &icon_buffer->scene_buffer->node;
		wlr_scene_node_set_position(icon_part->node, icon_padding, 0);
		button->window_icon = icon_buffer;
	} else {
		for (uint8_t state_set = LAB_BS_DEFAULT;
				state_set <= LAB_BS_ALL; state_set++) {
			if (!imgs[state_set].valid()) {
				continue;
			}
			struct ssd_part *icon_part = add_scene_part(part_list, type);
			auto img_buffer = new scaled_img_buffer(parent,
				imgs[state_set], g_theme.window_button_width,
				g_theme.window_button_height);
			icon_part->node = &img_buffer->scene_buffer->node;
			wlr_scene_node_set_enabled(icon_part->node, false);
			button->img_buffers[state_set] = img_buffer;
		}
		/* Initially show non-hover, non-toggled, unrounded variant */
		wlr_scene_node_set_enabled(
			&button->img_buffers[LAB_BS_DEFAULT]->scene_buffer->node, true);
	}

	return button_root;
}

struct ssd_part *
ssd_get_part(struct wl_list *part_list, enum lab_node_type type)
{
	struct ssd_part *part;
	wl_list_for_each(part, part_list, link) {
		if (part->type == type) {
			return part;
		}
	}
	return NULL;
}

void
ssd_destroy_parts(struct wl_list *list)
{
	struct ssd_part *part, *tmp;
	wl_list_for_each_reverse_safe(part, tmp, list, link) {
		if (part->node) {
			wlr_scene_node_destroy(part->node);
			part->node = NULL;
		}
		/* part->buffer will free itself along the scene_buffer node */
		part->buffer = NULL;
		wl_list_remove(&part->link);
		free(part);
	}
	assert(wl_list_empty(list));
}
