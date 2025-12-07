// SPDX-License-Identifier: GPL-2.0-only
#include "node.h"
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include "view.h"

void
node_descriptor_create(struct wlr_scene_node *scene_node,
		enum lab_node_type type, struct view *view, node_data_ptr data)
{
	auto node_descriptor = new struct node_descriptor();
	node_descriptor->type = type;
	node_descriptor->view.reset(view);
	node_descriptor->data = data;
	CONNECT_LISTENER(scene_node, node_descriptor, destroy);
	scene_node->data = node_descriptor;
}

struct view *
node_view_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (struct node_descriptor *)wlr_scene_node->data;
	return node_descriptor->view.get();
}

node_data_ptr
node_data_from_node(struct wlr_scene_node *wlr_scene_node)
{
	assert(wlr_scene_node->data);
	auto node_descriptor = (struct node_descriptor *)wlr_scene_node->data;
	return node_descriptor->data;
}
