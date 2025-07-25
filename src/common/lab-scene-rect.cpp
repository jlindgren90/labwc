// SPDX-License-Identifier: GPL-2.0-only
#include "common/lab-scene-rect.h"
#include <assert.h>
#include <wlr/types/wlr_scene.h>

struct lab_scene_rect *
lab_scene_rect_create(struct wlr_scene_tree *parent,
		struct lab_scene_rect_options *opts)
{
	auto rect = new lab_scene_rect();
	rect->border_width = opts->border_width;
	rect->nr_borders = opts->nr_borders;
	rect->borders.resize(opts->nr_borders);
	rect->tree = wlr_scene_tree_create(parent);

	if (opts->bg_color) {
		rect->fill = wlr_scene_rect_create(rect->tree, 0, 0, opts->bg_color);
	}

	for (int i = 0; i < rect->nr_borders; i++) {
		struct border_scene *border = &rect->borders[i];
		float *color = opts->border_colors[i];
		border->tree = wlr_scene_tree_create(rect->tree);
		border->top = wlr_scene_rect_create(border->tree, 0, 0, color);
		border->right = wlr_scene_rect_create(border->tree, 0, 0, color);
		border->bottom = wlr_scene_rect_create(border->tree, 0, 0, color);
		border->left = wlr_scene_rect_create(border->tree, 0, 0, color);
	}

	CONNECT_LISTENER(&rect->tree->node, rect, destroy);

	lab_scene_rect_set_size(rect, opts->width, opts->height);

	return rect;
}

static void
resize_border(struct border_scene *border, int border_width, int width, int height)
{
	/*
	 * The border is drawn like below:
	 *
	 * <--width-->
	 * +---------+   ^
	 * +-+-----+-+   |
	 * | |     | | height
	 * | |     | |   |
	 * +-+-----+-+   |
	 * +---------+   v
	 */

	if ((width < border_width * 2) || (height < border_width * 2)) {
		wlr_scene_node_set_enabled(&border->tree->node, false);
		return;
	}
	wlr_scene_node_set_enabled(&border->tree->node, true);

	wlr_scene_node_set_position(&border->top->node, 0, 0);
	wlr_scene_node_set_position(&border->bottom->node, 0, height - border_width);
	wlr_scene_node_set_position(&border->left->node, 0, border_width);
	wlr_scene_node_set_position(&border->right->node, width - border_width, border_width);

	wlr_scene_rect_set_size(border->top, width, border_width);
	wlr_scene_rect_set_size(border->bottom, width, border_width);
	wlr_scene_rect_set_size(border->left, border_width, height - border_width * 2);
	wlr_scene_rect_set_size(border->right, border_width, height - border_width * 2);
}

void
lab_scene_rect_set_size(struct lab_scene_rect *rect, int width, int height)
{
	assert(rect);
	int border_width = rect->border_width;

	for (int i = 0; i < rect->nr_borders; i++) {
		struct border_scene *border = &rect->borders[i];
		resize_border(border, border_width,
			width - 2 * border_width * i,
			height - 2 * border_width * i);
		wlr_scene_node_set_position(&border->tree->node,
			i * border_width, i * border_width);
	}

	if (rect->fill) {
		wlr_scene_rect_set_size(rect->fill, width, height);
	}
}
