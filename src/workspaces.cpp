// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "workspaces.h"
#include <assert.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <functional>
#include "buffer.h"
#include "common/font.h"
#include "common/graphic-helpers.h"
#include "config/rcxml.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "output.h"
#include "protocols/cosmic-workspaces.h"
#include "protocols/ext-workspace.h"
#include "theme.h"
#include "view.h"

#define COSMIC_WORKSPACES_VERSION 1
#define EXT_WORKSPACES_VERSION 1

/* Internal helpers */
static size_t
parse_workspace_index(const char *name)
{
	/*
	 * We only want to get positive numbers which span the whole string.
	 *
	 * More detailed requirement:
	 *  .---------------.--------------.
	 *  |     Input     | Return value |
	 *  |---------------+--------------|
	 *  | "2nd desktop" |      0       |
	 *  |    "-50"      |      0       |
	 *  |     "0"       |      0       |
	 *  |    "124"      |     124      |
	 *  |    "1.24"     |      0       |
	 *  `------------------------------´
	 *
	 * As atoi() happily parses any numbers until it hits a non-number we
	 * can't really use it for this case. Instead, we use strtol() combined
	 * with further checks for the endptr (remaining non-number characters)
	 * and returned negative numbers.
	 */
	long index;
	char *endptr;
	errno = 0;
	index = strtol(name, &endptr, 10);
	if (errno || *endptr != '\0' || index < 0) {
		return 0;
	}
	return index;
}

static void
_osd_update(void)
{
	/* Settings */
	uint16_t margin = 10;
	uint16_t padding = 2;
	uint16_t rect_height = g_theme.osd_workspace_switcher_boxes_height;
	uint16_t rect_width = g_theme.osd_workspace_switcher_boxes_width;
	bool hide_boxes = g_theme.osd_workspace_switcher_boxes_width == 0
		|| g_theme.osd_workspace_switcher_boxes_height == 0;

	/* Dimensions */
	size_t workspace_count = g_server.workspaces.all.size();
	uint16_t marker_width = workspace_count * (rect_width + padding) - padding;
	uint16_t width = margin * 2 + (marker_width < 200 ? 200 : marker_width);
	uint16_t height = margin * (hide_boxes ? 2 : 3) + rect_height + font_height(&rc.font_osd);

	for (auto &output : g_server.outputs) {
		if (!output_is_usable(&output)) {
			continue;
		}
		auto buffer = buffer_create_cairo(width, height,
			output.wlr_output->scale);
		cairo_t *cairo = cairo_create(buffer->surface);

		/* Background */
		set_cairo_color(cairo, g_theme.osd_bg_color);
		cairo_rectangle(cairo, 0, 0, width, height);
		cairo_fill(cairo);

		/* Border */
		set_cairo_color(cairo, g_theme.osd_border_color);
		struct wlr_fbox border_fbox = {
			.width = width,
			.height = height,
		};
		draw_cairo_border(cairo, border_fbox, g_theme.osd_border_width);

		/* Boxes */
		uint16_t x;
		if (!hide_boxes) {
			x = (width - marker_width) / 2;
			for (auto &workspace : g_server.workspaces.all) {
				bool active = &workspace
					== g_server.workspaces.current;
				set_cairo_color(cairo,
					g_theme.osd_label_text_color);
				struct wlr_fbox fbox = {
					.x = x,
					.y = margin,
					.width = rect_width,
					.height = rect_height,
				};
				draw_cairo_border(cairo, fbox,
					g_theme.osd_workspace_switcher_boxes_border_width);
				if (active) {
					cairo_rectangle(cairo, x, margin,
						rect_width, rect_height);
					cairo_fill(cairo);
				}
				x += rect_width + padding;
			}
		}

		/* Text */
		set_cairo_color(cairo, g_theme.osd_label_text_color);
		PangoLayout *layout = pango_cairo_create_layout(cairo);
		pango_context_set_round_glyph_positions(pango_layout_get_context(layout), false);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);

		/* Center workspace indicator on the x axis */
		ASSERT_PTR(g_server.workspaces.current, current);
		int req_width = font_width(&rc.font_osd, current->name.c());
		req_width = MIN(req_width, width - 2 * margin);
		x = (width - req_width) / 2;
		if (!hide_boxes) {
			cairo_move_to(cairo, x, margin * 2 + rect_height);
		} else {
			cairo_move_to(cairo, x, (height - font_height(&rc.font_osd)) / 2.0);
		}
		PangoFontDescription *desc = font_to_pango_desc(&rc.font_osd);
		//pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
		pango_layout_set_font_description(layout, desc);
		pango_layout_set_width(layout, req_width * PANGO_SCALE);
		pango_font_description_free(desc);
		pango_layout_set_text(layout, current->name.c(), -1);
		pango_cairo_show_layout(cairo, layout);

		g_object_unref(layout);
		cairo_surface_flush(cairo_get_target(cairo));
		cairo_destroy(cairo);

		if (!output.workspace_osd) {
			output.workspace_osd =
				wlr_scene_buffer_create(&g_server.scene->tree,
					NULL);
		}
		/* Position the whole thing */
		struct wlr_box output_box;
		wlr_output_layout_get_box(g_server.output_layout,
			output.wlr_output, &output_box);
		int lx = output.usable_area.x
			+ (output.usable_area.width - width) / 2 + output_box.x;
		int ly = output.usable_area.y
			+ (output.usable_area.height - height) / 2
			+ output_box.y;
		wlr_scene_node_set_position(&output.workspace_osd->node, lx, ly);
		wlr_scene_buffer_set_buffer(output.workspace_osd, buffer.get());
		wlr_scene_buffer_set_dest_size(output.workspace_osd,
			buffer->logical_width, buffer->logical_height);
	}
}

/* cosmic workspace handlers */
static void
handle_cosmic_workspace_activate(struct wl_listener *listener, void *data)
{
	struct workspace *workspace = wl_container_of(listener, workspace, on_cosmic.activate);
	workspaces_switch_to(workspace, /* update_focus */ true);
	wlr_log(WLR_INFO, "cosmic activating workspace %s",
		workspace->name.c());
}

/* ext workspace handlers */
static void
handle_ext_workspace_activate(struct wl_listener *listener, void *data)
{
	struct workspace *workspace = wl_container_of(listener, workspace, on_ext.activate);
	workspaces_switch_to(workspace, /* update_focus */ true);
	wlr_log(WLR_INFO, "ext activating workspace %s", workspace->name.c());
}

/* Internal API */
static void
add_workspace(const char *name)
{
	struct workspace *workspace = new ::workspace{};
	workspace->name = lab_str(name);
	workspace->tree = wlr_scene_tree_create(g_server.view_tree);
	g_server.workspaces.all.append(workspace);
	if (!g_server.workspaces.current) {
		g_server.workspaces.current.reset(workspace);
	} else {
		wlr_scene_node_set_enabled(&workspace->tree->node, false);
	}

	bool active = g_server.workspaces.current == workspace;

	/* cosmic */
	workspace->cosmic_workspace =
		lab_cosmic_workspace_create(g_server.workspaces.cosmic_group);
	lab_cosmic_workspace_set_name(workspace->cosmic_workspace, name);
	lab_cosmic_workspace_set_active(workspace->cosmic_workspace, active);

	workspace->on_cosmic.activate.notify = handle_cosmic_workspace_activate;
	wl_signal_add(&workspace->cosmic_workspace->events.activate,
		&workspace->on_cosmic.activate);

	/* ext */
	workspace->ext_workspace =
		lab_ext_workspace_create(g_server.workspaces.ext_manager,
			/*id*/ NULL);
	lab_ext_workspace_assign_to_group(workspace->ext_workspace,
		g_server.workspaces.ext_group);
	lab_ext_workspace_set_name(workspace->ext_workspace, name);
	lab_ext_workspace_set_active(workspace->ext_workspace, active);

	workspace->on_ext.activate.notify = handle_ext_workspace_activate;
	wl_signal_add(&workspace->ext_workspace->events.activate,
		&workspace->on_ext.activate);
}

bool
workspace::has_views()
{
	for_each_view(view, g_views.begin(), LAB_VIEW_CRITERIA_NO_OMNIPRESENT) {
		if (view->workspace.get() == this) {
			return true;
		}
	}
	return false;
}

static struct workspace *
get_adjacent_occupied(struct workspace *current, ownlist<workspace> &workspaces,
		bool wrap, bool reverse)
{
	auto start = reverse ? workspaces.rbegin() : workspaces.begin();
	auto stop = reverse ? workspaces.rend() : workspaces.end();

	return lab::next_after_if(start, stop, current, wrap,
		std::mem_fn(&workspace::has_views)).get();
}

static struct workspace *
get_prev_occupied(struct workspace *current, ownlist<workspace> &workspaces,
		bool wrap)
{
	return get_adjacent_occupied(current, workspaces, wrap, true);
}

static struct workspace *
get_next_occupied(struct workspace *current, ownlist<workspace> &workspaces,
		bool wrap)
{
	return get_adjacent_occupied(current, workspaces, wrap, false);
}

static int
_osd_handle_timeout(void *data)
{
	workspaces_osd_hide();
	/* Don't re-check */
	return 0;
}

static void
_osd_show(void)
{
	if (!rc.workspace_config.popuptime) {
		return;
	}

	_osd_update();
	for (auto &output : g_server.outputs) {
		if (output_is_usable(&output) && output.workspace_osd) {
			wlr_scene_node_set_enabled(&output.workspace_osd->node,
				true);
		}
	}
	if (keyboard_get_all_modifiers()) {
		/* Hidden by release of all modifiers */
		g_seat.workspace_osd_shown_by_modifier = true;
	} else {
		/* Hidden by timer */
		if (!g_seat.workspace_osd_timer) {
			g_seat.workspace_osd_timer =
				wl_event_loop_add_timer(g_server.wl_event_loop,
					_osd_handle_timeout, NULL);
		}
		wl_event_source_timer_update(g_seat.workspace_osd_timer,
			rc.workspace_config.popuptime);
	}
}

/* Public API */
void
workspaces_init(void)
{
	g_server.workspaces.cosmic_manager =
		lab_cosmic_workspace_manager_create(g_server.wl_display,
			/* capabilities */ CW_CAP_WS_ACTIVATE,
			COSMIC_WORKSPACES_VERSION);

	g_server.workspaces.ext_manager =
		lab_ext_workspace_manager_create(g_server.wl_display,
			/* capabilities */ WS_CAP_WS_ACTIVATE,
			EXT_WORKSPACES_VERSION);

	g_server.workspaces.cosmic_group = lab_cosmic_workspace_group_create(
		g_server.workspaces.cosmic_manager);

	g_server.workspaces.ext_group =
		lab_ext_workspace_group_create(g_server.workspaces.ext_manager);

	for (auto name : rc.workspace_config.names) {
		add_workspace(name.c());
	}
}

/*
 * update_focus should normally be set to true. It is set to false only
 * when this function is called from desktop_focus_view(), in order to
 * avoid unnecessary extra focus changes and possible recursion.
 */
void
workspaces_switch_to(struct workspace *target, bool update_focus)
{
	assert(target);
	if (target == g_server.workspaces.current) {
		return;
	}

	/* Disable the old workspace */
	ASSERT_PTR(g_server.workspaces.current, old);
	wlr_scene_node_set_enabled(&old->tree->node, false);

	lab_cosmic_workspace_set_active(old->cosmic_workspace, false);
	lab_ext_workspace_set_active(old->ext_workspace, false);

	/* Move Omnipresent views to new workspace */
	enum lab_view_criteria criteria =
		LAB_VIEW_CRITERIA_CURRENT_WORKSPACE;
	for_each_view(view, g_views.rbegin(), criteria) {
		if (view->visible_on_all_workspaces) {
			view_move_to_workspace(view.get(), target);
		}
	}

	/* Enable the new workspace */
	wlr_scene_node_set_enabled(&target->tree->node, true);

	/* Save the last visited workspace */
	g_server.workspaces.last = g_server.workspaces.current;

	/* Make sure new views will spawn on the new workspace */
	g_server.workspaces.current.reset(target);

	struct view *grabbed_view = g_server.grabbed_view;
	if (grabbed_view && !view_is_always_on_top(grabbed_view)) {
		view_move_to_workspace(grabbed_view, target);
	}

	/*
	 * Make sure we are focusing what the user sees. Only refocus if
	 * the focus is not already on an omnipresent or always-on-top view.
	 *
	 * TODO: Decouple always-on-top views from the omnipresent state.
	 *       One option for that would be to create a new scene tree
	 *       as child of every workspace tree and then reparent a-o-t
	 *       windows to that one. Combined with adjusting the condition
	 *       below that should take care of the issue.
	 */
	if (update_focus) {
		struct view *active_view = g_server.active_view;
		if (!active_view || (!active_view->visible_on_all_workspaces
				&& !view_is_always_on_top(active_view))) {
			desktop_focus_topmost_view();
		}
	}

	/* And finally show the OSD */
	_osd_show();

	/*
	 * Make sure we are not carrying around a
	 * cursor image from the previous desktop
	 */
	cursor_update_focus();

	/* Ensure that only currently visible fullscreen windows hide the top layer */
	desktop_update_top_layer_visibility();

	lab_cosmic_workspace_set_active(target->cosmic_workspace, true);
	lab_ext_workspace_set_active(target->ext_workspace, true);
}

void
workspaces_osd_hide(void)
{
	for (auto &output : g_server.outputs) {
		if (!output.workspace_osd) {
			continue;
		}
		wlr_scene_node_set_enabled(&output.workspace_osd->node, false);
		wlr_scene_buffer_set_buffer(output.workspace_osd, NULL);
	}
	g_seat.workspace_osd_shown_by_modifier = false;

	/* Update the cursor focus in case it was on top of the OSD before */
	cursor_update_focus();
}

struct workspace *
workspaces_find(struct workspace *anchor, const char *name, bool wrap)
{
	assert(anchor);
	if (!name) {
		return NULL;
	}
	size_t index = 0;
	size_t wants_index = parse_workspace_index(name);
	auto &workspaces = g_server.workspaces.all;

	if (wants_index) {
		for (auto &target : workspaces) {
			if (wants_index == ++index) {
				return &target;
			}
		}
	} else if (!strcasecmp(name, "current")) {
		return anchor;
	} else if (!strcasecmp(name, "last")) {
		return g_server.workspaces.last.get();
	} else if (!strcasecmp(name, "left")) {
		return lab::next_after(workspaces.rbegin(), anchor, wrap).get();
	} else if (!strcasecmp(name, "right")) {
		return lab::next_after(workspaces.begin(), anchor, wrap).get();
	} else if (!strcasecmp(name, "left-occupied")) {
		return get_prev_occupied(anchor, workspaces, wrap);
	} else if (!strcasecmp(name, "right-occupied")) {
		return get_next_occupied(anchor, workspaces, wrap);
	} else {
		for (auto &target : workspaces) {
			if (!strcasecmp(target.name.c(), name)) {
				return &target;
			}
		}
	}
	wlr_log(WLR_ERROR, "Workspace '%s' not found", name);
	return NULL;
}

workspace::~workspace()
{
	auto workspace = this;
	wlr_scene_node_destroy(&workspace->tree->node);
	wl_list_remove(&workspace->on_cosmic.activate.link);
	wl_list_remove(&workspace->on_ext.activate.link);

	lab_cosmic_workspace_destroy(workspace->cosmic_workspace);
	lab_ext_workspace_destroy(workspace->ext_workspace);
}

void
workspaces_reconfigure(void)
{
	/*
	 * Compare actual workspace list with the new desired configuration to:
	 *   - Update names
	 *   - Add workspaces if more workspaces are desired
	 *   - Destroy workspaces if fewer workspace are desired
	 */

	auto actual_workspace = g_server.workspaces.all.begin();

	for (auto configured_name : rc.workspace_config.names) {
		if (!actual_workspace) {
			/* # of configured workspaces increased */
			wlr_log(WLR_DEBUG, "Adding workspace \"%s\"",
				configured_name.c());
			add_workspace(configured_name.c());
			continue;
		}

		if (actual_workspace->name != configured_name) {
			/* Workspace is renamed */
			wlr_log(WLR_DEBUG,
				"Renaming workspace \"%s\" to \"%s\"",
				actual_workspace->name.c(),
				configured_name.c());
			actual_workspace->name = configured_name;
			lab_cosmic_workspace_set_name(
				actual_workspace->cosmic_workspace,
				actual_workspace->name.c());
			lab_ext_workspace_set_name(
				actual_workspace->ext_workspace,
				actual_workspace->name.c());
		}
		++actual_workspace;
	}

	if (!actual_workspace) {
		return;
	}

	/* # of configured workspaces decreased */
	overlay_hide();
	auto first_workspace = g_server.workspaces.all.begin();

	while (actual_workspace) {
		wlr_log(WLR_DEBUG, "Destroying workspace \"%s\"",
			actual_workspace->name.c());

		for (auto &view : g_views) {
			if (view.workspace.get() == actual_workspace.get()) {
				view_move_to_workspace(&view,
					first_workspace.get());
			}
		}

		if (g_server.workspaces.current == actual_workspace.get()) {
			workspaces_switch_to(first_workspace.get(),
				/* update_focus */ true);
		}
		if (g_server.workspaces.last == actual_workspace.get()) {
			g_server.workspaces.last.reset(first_workspace.get());
		}

		actual_workspace.remove();
		++actual_workspace;
	}
}

void
workspaces_destroy(void)
{
	g_server.workspaces.current.reset();
	g_server.workspaces.last.reset();
	g_server.workspaces.all.clear();
}
