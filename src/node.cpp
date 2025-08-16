// SPDX-License-Identifier: GPL-2.0-only
#include "node.h"
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>

void
node_descriptor_create(struct wlr_scene_node *scene_node,
		enum node_descriptor_type type, void *data)
{
	auto node_descriptor = new ::node_descriptor();
	node_descriptor->type = type;
	node_descriptor->data = data;
	CONNECT_LISTENER(scene_node, node_descriptor, destroy);
	scene_node->data = node_descriptor;
}

struct view *
node_view_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (::node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_DESC_VIEW
		|| node_descriptor->type == LAB_NODE_DESC_XDG_POPUP);
	return (struct view *)node_descriptor->data;
}

struct lab_layer_surface *
node_layer_surface_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (::node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_DESC_LAYER_SURFACE);
	return (struct lab_layer_surface *)node_descriptor->data;
}

struct lab_layer_popup *
node_layer_popup_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (::node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_DESC_LAYER_POPUP);
	return (struct lab_layer_popup *)node_descriptor->data;
}

struct menuitem *
node_menuitem_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (::node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_DESC_MENUITEM);
	return (struct menuitem *)node_descriptor->data;
}

struct ssd_button *
node_ssd_button_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (::node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_DESC_SSD_BUTTON);
	return (struct ssd_button *)node_descriptor->data;
}

struct scaled_scene_buffer *
node_scaled_scene_buffer_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (::node_descriptor *)wlr_scene_node->data;
	assert(node_descriptor->type == LAB_NODE_DESC_SCALED_SCENE_BUFFER);
	return (struct scaled_scene_buffer *)node_descriptor->data;
}
