// SPDX-License-Identifier: GPL-2.0-only
#include "node.h"
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>

void
node_descriptor_create(struct wlr_scene_node *scene_node,
		enum lab_node_type type, struct view *view, void *data)
{
	auto node_descriptor = new struct node_descriptor();
	node_descriptor->type = type;
	node_descriptor->view = view;
	node_descriptor->data = data;
	CONNECT_LISTENER(scene_node, node_descriptor, destroy);
	scene_node->data = node_descriptor;
}

struct view *
node_view_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (struct node_descriptor *)wlr_scene_node->data;
	return node_descriptor->view;
}

struct lab_layer_surface *
node_layer_surface_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (struct node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_LAYER_SURFACE);
	return (struct lab_layer_surface *)node_descriptor->data;
}

struct menuitem *
node_menuitem_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (struct node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_MENUITEM);
	return (struct menuitem *)node_descriptor->data;
}

struct ssd_button *
node_try_ssd_button_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (struct node_descriptor *)wlr_scene_node->data;

	if (node_type_contains(LAB_NODE_BUTTON, node_descriptor->type)) {
		return (struct ssd_button *)node_descriptor->data;
	}

	return NULL;
}
