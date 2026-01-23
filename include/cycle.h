/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_CYCLE_H
#define LABWC_CYCLE_H

#include <stdbool.h>
#include <wayland-server-core.h>

struct output;

enum lab_cycle_dir {
	LAB_CYCLE_DIR_NONE,
	LAB_CYCLE_DIR_FORWARD,
	LAB_CYCLE_DIR_BACKWARD,
};

struct buf;
struct view;
struct server;
struct wlr_scene_node;

/* Begin window switcher */
void cycle_begin(enum lab_cycle_dir direction);

/* Cycle the selected view in the window switcher */
void cycle_step(enum lab_cycle_dir direction);

/* Closes the OSD */
void cycle_finish(bool switch_focus);

/* Focus the clicked window and close OSD */
void cycle_on_cursor_release(struct wlr_scene_node *node);

/* Internal API */
struct cycle_osd_item {
	int cycle_idx;
	struct wlr_scene_tree *tree;
	struct wl_list link;
};

/*
 * Create a scene-tree of OSD for an output.
 * This sets output->cycle_osd.{items,tree}.
 */
void cycle_osd_classic_create(struct output *output);
/*
 * Update output->cycle_osd.tree to highlight
 * server->cycle_state.selected_view.
 */
void cycle_osd_classic_update(struct output *output);

#endif // LABWC_CYCLE_H
