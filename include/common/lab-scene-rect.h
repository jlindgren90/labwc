/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_LAB_SCENE_RECT_H
#define LABWC_LAB_SCENE_RECT_H

#include <vector>
#include "listener.h"

struct wlr_scene_tree;

struct lab_scene_rect_options {
	float **border_colors;
	int nr_borders;
	int border_width;
	float *bg_color; /* can be NULL */
	int width;
	int height;
};

struct border_scene {
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *top, *bottom, *left, *right;
};

struct lab_scene_rect : public destroyable {
	struct wlr_scene_tree *tree;
	int border_width;
	int nr_borders;
	std::vector<border_scene> borders;
	struct wlr_scene_rect *fill;
};

/**
 * Create a new rectangle with borders.
 *
 * The rectangle can be positioned by positioning border_rect->tree->node.
 *
 * It can be destroyed by destroying its tree node (or one of its parent nodes).
 * Once the tree node has been destroyed the struct will be free'd automatically.
 */
struct lab_scene_rect *lab_scene_rect_create(struct wlr_scene_tree *parent,
	struct lab_scene_rect_options *opts);

void lab_scene_rect_set_size(struct lab_scene_rect *rect, int width, int height);

#endif /* LABWC_LAB_SCENE_RECT_H */
