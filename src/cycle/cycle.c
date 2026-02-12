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

/* Returns the view to select next in the window switcher. */
static struct view *
get_next_selected_view(enum lab_cycle_dir dir)
{
	struct cycle_state *cycle = &g_server.cycle;
	assert(cycle->selected_view);
	assert(!wl_list_empty(&g_server.cycle.views));

	struct wl_list *link;
	if (dir == LAB_CYCLE_DIR_FORWARD) {
		link = cycle->selected_view->cycle_link.next;
		if (link == &g_server.cycle.views) {
			link = link->next;
		}
	} else {
		link = cycle->selected_view->cycle_link.prev;
		if (link == &g_server.cycle.views) {
			link = link->prev;
		}
	}
	struct view *view = wl_container_of(link, view, cycle_link);
	return view;
}

static struct view *
get_first_view(struct wl_list *views)
{
	assert(!wl_list_empty(views));
	struct view *view = wl_container_of(views->next, view, cycle_link);
	return view;
}

void
cycle_reinitialize(void)
{
	struct cycle_state *cycle = &g_server.cycle;

	if (g_server.input_mode != LAB_INPUT_STATE_CYCLE) {
		/* OSD not active, no need for clean up */
		return;
	}

	struct view *selected_view = cycle->selected_view;
	struct view *selected_view_prev =
		get_next_selected_view(LAB_CYCLE_DIR_BACKWARD);

	destroy_cycle();
	if (init_cycle()) {
		/*
		 * Preserve the selected view (or its previous view) if it's
		 * still in the cycle list
		 */
		if (selected_view->cycle_link.next) {
			cycle->selected_view = selected_view;
		} else if (selected_view_prev->cycle_link.next) {
			cycle->selected_view = selected_view_prev;
		} else {
			/* should be unreachable */
			wlr_log(WLR_ERROR, "could not find view to select");
			cycle->selected_view =
				get_first_view(&g_server.cycle.views);
		}
		update_cycle();
	} else {
		/* Failed to re-init window switcher, exit */
		cycle_finish(/*switch_focus*/ false);
	}
}

void
cycle_on_cursor_release(struct wlr_scene_node *node)
{
	assert(g_server.input_mode == LAB_INPUT_STATE_CYCLE);

	struct cycle_osd_item *item = node_cycle_osd_item_from_node(node);
	g_server.cycle.selected_view = item->view;
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

	struct view *active_view = view_get_active();
	if (active_view && active_view->cycle_link.next) {
		/* Select the active view it's in the cycle list */
		g_server.cycle.selected_view = active_view;
	} else {
		/* Otherwise, select the first view in the cycle list */
		g_server.cycle.selected_view =
			get_first_view(&g_server.cycle.views);
	}
	/* Pre-select the next view in the given direction */
	g_server.cycle.selected_view = get_next_selected_view(direction);

	seat_focus_override_begin(LAB_INPUT_STATE_CYCLE, LAB_CURSOR_DEFAULT);
	update_cycle();

	/* Update cursor, in case it is within the area covered by OSD */
	cursor_update_focus();
}

void
cycle_step(enum lab_cycle_dir direction)
{
	assert(g_server.input_mode == LAB_INPUT_STATE_CYCLE);

	g_server.cycle.selected_view = get_next_selected_view(direction);
	update_cycle();
}

void
cycle_finish(bool switch_focus)
{
	if (g_server.input_mode != LAB_INPUT_STATE_CYCLE) {
		return;
	}

	struct view *selected_view = g_server.cycle.selected_view;
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
	for (int i = view_count() - 1; i >= 0; i--) {
		struct view *view = view_nth(i);
		if (!view_is_focusable(view->st) || view != view_get_root(view->id)) {
			continue;
		}
		wl_list_append(&g_server.cycle.views, &view->cycle_link);
	}
	if (wl_list_empty(&g_server.cycle.views)) {
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

	struct view *view, *tmp;
	wl_list_for_each_safe(view, tmp, &g_server.cycle.views, cycle_link) {
		wl_list_remove(&view->cycle_link);
		view->cycle_link = (struct wl_list){0};
	}

	g_server.cycle.selected_view = NULL;
}
