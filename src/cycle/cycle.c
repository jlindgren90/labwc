// SPDX-License-Identifier: GPL-2.0-only
#include "cycle.h"
#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "common/lab-scene-rect.h"
#include "common/list.h"
#include "common/scene-helpers.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "ssd.h"
#include "view.h"

static bool init_cycle(void);
static void update_cycle(void);
static void destroy_cycle(void);

void
cycle_on_cursor_release(struct wlr_scene_node *node)
{
	assert(g_server.input_mode == LAB_INPUT_STATE_CYCLE);

	struct cycle_osd_item *item = node_cycle_osd_item_from_node(node);
	g_server.current_cycle_idx = item->cycle_idx;
	cycle_finish(/*switch_focus*/ true);
}

void
cycle_begin(enum lab_cycle_dir direction)
{
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	if (!init_cycle()) {
		return;
	}

	int len = cycle_list_len();
	assert(len > 0);

	if (direction == LAB_CYCLE_DIR_FORWARD) {
		if (len > 1 && cycle_list_nth(0) == view_get_active()) {
			g_server.current_cycle_idx = 1;
		} else {
			g_server.current_cycle_idx = 0;
		}
	} else {
		g_server.current_cycle_idx = len - 1;
	}

	seat_focus_override_begin(LAB_INPUT_STATE_CYCLE, LAB_CURSOR_DEFAULT);
	update_cycle();

	/* Update cursor, in case it is within the area covered by OSD */
	cursor_update_focus();
}

void
cycle_step(enum lab_cycle_dir direction)
{
	assert(g_server.input_mode == LAB_INPUT_STATE_CYCLE);

	int len = cycle_list_len();
	assert(len > 0);

	if (direction == LAB_CYCLE_DIR_FORWARD) {
		g_server.current_cycle_idx =
			(g_server.current_cycle_idx + 1) % len;
	} else {
		g_server.current_cycle_idx =
			(g_server.current_cycle_idx + len - 1) % len;
	}

	update_cycle();
}

void
cycle_finish(bool switch_focus)
{
	if (g_server.input_mode != LAB_INPUT_STATE_CYCLE) {
		return;
	}

	struct view *selected_view = cycle_list_nth(g_server.current_cycle_idx);
	destroy_cycle();

	/* FIXME: this sets focus to the old surface even with switch_focus=true */
	seat_focus_override_end();

	/* Hiding OSD may need a cursor change */
	cursor_update_focus();

	if (switch_focus && selected_view) {
		view_focus(selected_view->id, /*raise*/ true);
	}
}

static void
create_osd_on_output(struct output *output)
{
	if (!output_is_usable(output)) {
		return;
	}
	cycle_osd_classic_create(output);
	assert(output->cycle_osd.tree);
}

/* Return false on failure */
static bool
init_cycle(void)
{
	cycle_list_build();
	if (cycle_list_len() <= 0) {
		wlr_log(WLR_DEBUG, "no views to switch between");
		return false;
	}

	/* Create OSD */
	struct output *output;
	wl_list_for_each(output, &g_server.outputs, link) {
		create_osd_on_output(output);
	}

	return true;
}

static void
update_cycle(void)
{
	struct output *output;
	wl_list_for_each(output, &g_server.outputs, link) {
		if (output->cycle_osd.tree) {
			cycle_osd_classic_update(output);
		}
	}
}

/* Resets all the states in g_server.cycle */
static void
destroy_cycle(void)
{
	struct output *output;
	wl_list_for_each(output, &g_server.outputs, link) {
		struct cycle_osd_item *item, *tmp;
		wl_list_for_each_safe(item, tmp, &output->cycle_osd.items, link) {
			wl_list_remove(&item->link);
			free(item);
		}
		if (output->cycle_osd.tree) {
			wlr_scene_node_destroy(&output->cycle_osd.tree->node);
			output->cycle_osd.tree = NULL;
		}
	}
}
