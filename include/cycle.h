/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_CYCLE_H
#define LABWC_CYCLE_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct output;
struct wlr_box;

enum lab_cycle_dir {
	LAB_CYCLE_DIR_NONE,
	LAB_CYCLE_DIR_FORWARD,
	LAB_CYCLE_DIR_BACKWARD,
};

struct cycle_state {
	struct view *selected_view;
	struct wl_list views;
	struct wl_list osd_outputs; /* struct cycle_osd_output.link */
};

struct cycle_osd_output {
	struct wl_list link; /* struct cycle_state.osd_outputs */
	struct output *output;
	struct wl_listener tree_destroy;

	/* set by cycle_osd_impl->init() */
	struct wl_list items; /* struct cycle_osd_item.link */
	struct wlr_scene_tree *tree;
	/* set by cycle_osd_impl->init() and moved by cycle_osd_scroll_update() */
	struct wlr_scene_tree *items_tree;

	/* used in osd-scroll.c */
	struct cycle_osd_scroll_context {
		int top_row_idx;
		int nr_rows, nr_cols, nr_visible_rows;
		int delta_y;
		struct wlr_box bar_area;
		struct wlr_scene_tree *bar_tree;
		struct lab_scene_rect *bar;
	} scroll;
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

/* Re-initialize the window switcher */
void cycle_reinitialize(void);

/* Focus the clicked window and close OSD */
void cycle_on_cursor_release(struct wlr_scene_node *node);

/* Internal API */
struct cycle_osd_item {
	struct view *view;
	struct wlr_scene_tree *tree;
	struct wl_list link;
};

/*
 * Create a scene-tree of OSD for an output and fill
 * osd_output->items.
 */
void cycle_osd_classic_init(struct cycle_osd_output *osd_output);
/*
 * Update the OSD to highlight g_server.cycle.selected_view.
 */
void cycle_osd_classic_update(struct cycle_osd_output *osd_output);

#define SCROLLBAR_W 10

/**
 * Initialize the context and scene for scrolling OSD items.
 *
 * @output: Output of the OSD
 * @bar_area: Area where the scrollbar is drawn
 * @delta_y: The vertical delta by which items are scrolled (usually item height)
 * @nr_cols: Number of columns in the OSD
 * @nr_rows: Number of rows in the OSD
 * @nr_visible_rows: Number of visible rows in the OSD
 * @border_color: Border color of the scrollbar
 * @bg_color: Background color of the scrollbar
 */
void cycle_osd_scroll_init(struct cycle_osd_output *osd_output,
	struct wlr_box bar_area, int delta_y,
	int nr_cols, int nr_rows, int nr_visible_rows,
	float *border_color, float *bg_color);

/* Scroll the OSD to show g_server.cycle.selected_view if needed */
void cycle_osd_scroll_update(struct cycle_osd_output *osd_output);

#endif // LABWC_CYCLE_H
