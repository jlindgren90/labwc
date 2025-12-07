/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_NODE_DESCRIPTOR_H
#define LABWC_NODE_DESCRIPTOR_H

#include <variant>
#include <wayland-server-core.h>
#include "common/listener.h"
#include "common/node-type.h"
#include "common/refptr.h"

struct wlr_scene_node;

using node_data_ptr = std::variant<std::monostate, struct cycle_osd_item *,
	struct lab_layer_surface *, struct lab_layer_popup *, struct menuitem *,
	struct ssd_button *>;

struct node_descriptor : public destroyable {
	enum lab_node_type type;
	weakptr<struct view> view;
	node_data_ptr data;
};

/**
 * node_descriptor_create - create node descriptor for wlr_scene_node user_data
 *
 * The node_descriptor will be destroyed automatically
 * once the scene_node it is attached to is destroyed.
 *
 * @scene_node: wlr_scene_node to attached node_descriptor to
 * @type: node descriptor type
 * @view: associated view
 * @data: struct to point to as follows:
 *   - LAB_NODE_CYCLE_OSD_ITEM struct cycle_osd_item
 *   - LAB_NODE_LAYER_SURFACE  struct lab_layer_surface
 *   - LAB_NODE_LAYER_POPUP    struct lab_layer_popup
 *   - LAB_NODE_MENUITEM       struct menuitem
 *   - LAB_NODE_BUTTON_*       struct ssd_button
 */
void node_descriptor_create(struct wlr_scene_node *scene_node,
	enum lab_node_type type, struct view *view,
	node_data_ptr data = node_data_ptr());

/**
 * node_view_from_node - return view struct from node
 * @wlr_scene_node: wlr_scene_node from which to return data
 */
struct view *node_view_from_node(struct wlr_scene_node *wlr_scene_node);

node_data_ptr node_data_from_node(struct wlr_scene_node *wlr_scene_node);

#endif /* LABWC_NODE_DESCRIPTOR_H */
