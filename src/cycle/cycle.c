// SPDX-License-Identifier: GPL-2.0-only
#include "cycle.h"
#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "common/lab-scene-rect.h"
#include "common/list.h"
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "view.h"

static bool init_cycle(void);
static void update_cycle(void);
static void destroy_cycle(void);

void
cycle_on_cursor_release(struct wlr_scene_node *node)
{
	assert(g_server.input_mode == LAB_INPUT_STATE_CYCLE);

	struct cycle_osd_item *item = node_cycle_osd_item_from_node(node);
	g_server.cycle.current_idx = item->cycle_idx;
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
			g_server.cycle.current_idx = 1;
		} else {
			g_server.cycle.current_idx = 0;
		}
	} else {
		g_server.cycle.current_idx = len - 1;
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
		g_server.cycle.current_idx =
			(g_server.cycle.current_idx + 1) % len;
	} else {
		g_server.cycle.current_idx =
			(g_server.cycle.current_idx + len - 1) % len;
	}

	update_cycle();
}

void
cycle_finish(bool switch_focus)
{
	if (g_server.input_mode != LAB_INPUT_STATE_CYCLE) {
		return;
	}

	ViewId selected_id = cycle_list_nth(g_server.cycle.current_idx);
	destroy_cycle();

	seat_focus_override_end(/*restore_focus*/ false);

	/* Hiding OSD may need a cursor change */
	cursor_update_focus();

	if (switch_focus) {
		view_focus(selected_id, /*raise*/ true);
	}
}

static void
handle_osd_tree_destroy(struct wl_listener *listener, void *data)
{
	struct cycle_osd_output *osd_output =
		wl_container_of(listener, osd_output, tree_destroy);
	struct cycle_osd_item *item, *tmp;
	wl_list_for_each_safe(item, tmp, &osd_output->items, link) {
		wl_list_remove(&item->link);
		free(item);
	}
	wl_list_remove(&osd_output->tree_destroy.link);
	wl_list_remove(&osd_output->link);
	free(osd_output);
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
		if (!output_is_usable(output)) {
			continue;
		}

		struct cycle_osd_output *osd_output = znew(*osd_output);
		wl_list_append(&g_server.cycle.osd_outputs, &osd_output->link);
		osd_output->output = output;
		wl_list_init(&osd_output->items);

		cycle_osd_classic_init(osd_output);

		osd_output->tree_destroy.notify = handle_osd_tree_destroy;
		wl_signal_add(&osd_output->tree->node.events.destroy,
			&osd_output->tree_destroy);
	}

	return true;
}

static void
update_cycle(void)
{
	struct cycle_state *cycle = &g_server.cycle;

	struct cycle_osd_output *osd_output;
	wl_list_for_each(osd_output, &cycle->osd_outputs, link) {
		cycle_osd_classic_update(osd_output);
	}
}

/* Resets all the states in g_server.cycle */
static void
destroy_cycle(void)
{
	struct cycle_osd_output *osd_output, *tmp;
	wl_list_for_each_safe(osd_output, tmp, &g_server.cycle.osd_outputs, link) {
		/* calls handle_osd_tree_destroy() */
		wlr_scene_node_destroy(&osd_output->tree->node);
	}

	g_server.cycle = (struct cycle_state){0};
	wl_list_init(&g_server.cycle.osd_outputs);
}
