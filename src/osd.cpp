// SPDX-License-Identifier: GPL-2.0-only
#include "osd.h"
#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "common/array.h"
#include "common/buf.h"
#include "common/font.h"
#include "common/lab-scene-rect.h"
#include "common/scaled-font-buffer.h"
#include "common/scaled-icon-buffer.h"
#include "common/scene-helpers.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "output.h"
#include "ssd.h"
#include "theme.h"
#include "view.h"
#include "workspaces.h"

struct osd_scene_item {
	struct view *view;
	struct wlr_scene_node *highlight_outline;
};

static void update_osd(void);

static void
destroy_osd_scenes(void)
{
	struct output *output;
	wl_list_for_each(output, &g_server.outputs, link) {
		wlr_scene_node_destroy(&output->osd_scene.tree->node);
		output->osd_scene.tree = NULL;

		wl_array_release(&output->osd_scene.items);
		wl_array_init(&output->osd_scene.items);
	}
}

static void
osd_update_preview_outlines(struct view *view)
{
	/* Create / Update preview outline tree */
	struct lab_scene_rect *rect = g_server.osd_state.preview_outline;
	if (!rect) {
		struct lab_scene_rect_options opts = {
			.border_colors = (float *[3]) {
				g_theme.osd_window_switcher_preview_border_color[0],
				g_theme.osd_window_switcher_preview_border_color[1],
				g_theme.osd_window_switcher_preview_border_color[2],
			},
			.nr_borders = 3,
			.border_width =
				g_theme.osd_window_switcher_preview_border_width,
		};
		rect = lab_scene_rect_create(&g_server.scene->tree, &opts);
		wlr_scene_node_place_above(&rect->tree->node,
			&g_server.menu_tree->node);
		g_server.osd_state.preview_outline = rect;
	}

	struct wlr_box geo = ssd_max_extents(view);
	lab_scene_rect_set_size(rect, geo.width, geo.height);
	wlr_scene_node_set_position(&rect->tree->node, geo.x, geo.y);
}

/*
 * Returns the view to select next in the window switcher.
 * If !start_view, the second focusable view is returned.
 */
static struct view *
get_next_cycle_view(struct view *start_view, enum lab_cycle_dir dir)
{
	bool forwards = dir == LAB_CYCLE_DIR_FORWARD;
	auto begin = forwards ? g_views.begin() : g_views.rbegin();
	auto pos = lab::find_ptr(begin, start_view);

	enum lab_view_criteria criteria = rc.window_switcher.criteria;

	/*
	 * Views are listed in stacking order, topmost first.  Usually the
	 * topmost view is already focused, so when iterating in the forward
	 * direction we pre-select the view second from the top:
	 *
	 *   View #1 (on top, currently focused)
	 *   View #2 (pre-selected)
	 *   View #3
	 *   ...
	 */
	if (!pos && forwards) {
		pos = view_find_matching(begin, criteria); // top view
	}
	if (pos) {
		pos = view_find_matching(++pos, criteria); // next view
	}
	if (!pos) {
		pos = view_find_matching(begin, criteria); // wrap around
	}
	return pos.get();
}

void
osd_on_view_destroy(struct view *view)
{
	assert(view);
	struct osd_state *osd_state = &g_server.osd_state;

	if (g_server.input_mode != LAB_INPUT_STATE_WINDOW_SWITCHER) {
		/* OSD not active, no need for clean up */
		return;
	}

	if (osd_state->cycle_view == view) {
		/*
		 * If we are the current OSD selected view, cycle
		 * to the next because we are dying.
		 */

		/* Also resets preview node */
		osd_state->cycle_view =
			get_next_cycle_view(osd_state->cycle_view,
				LAB_CYCLE_DIR_BACKWARD);

		/*
		 * If we cycled back to ourselves, then we have no more windows.
		 * Just close the OSD for good.
		 */
		if (osd_state->cycle_view == view || !osd_state->cycle_view) {
			/* osd_finish() additionally resets cycle_view to NULL */
			osd_finish();
		}
	}

	if (osd_state->cycle_view) {
		/* Recreate the OSD to reflect the view has now gone. */
		destroy_osd_scenes();
		update_osd();
	}

	if (view->scene_tree) {
		struct wlr_scene_node *node = &view->scene_tree->node;
		if (osd_state->preview_anchor == node) {
			/*
			 * If we are the anchor for the current OSD selected view,
			 * replace the anchor with the node before us.
			 */
			osd_state->preview_anchor = lab_wlr_scene_get_prev_node(node);
		}
	}
}

static void
restore_preview_node(void)
{
	struct osd_state *osd_state = &g_server.osd_state;
	if (osd_state->preview_node) {
		wlr_scene_node_reparent(osd_state->preview_node,
			osd_state->preview_parent);

		if (osd_state->preview_anchor) {
			wlr_scene_node_place_above(osd_state->preview_node,
				osd_state->preview_anchor);
		} else {
			/* Selected view was the first node */
			wlr_scene_node_lower_to_bottom(osd_state->preview_node);
		}

		/* Node was disabled / minimized before, disable again */
		if (!osd_state->preview_was_enabled) {
			wlr_scene_node_set_enabled(osd_state->preview_node, false);
		}
		osd_state->preview_node = NULL;
		osd_state->preview_parent = NULL;
		osd_state->preview_anchor = NULL;
	}
}

void
osd_begin(enum lab_cycle_dir direction)
{
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	g_server.osd_state.cycle_view =
		get_next_cycle_view(g_server.osd_state.cycle_view, direction);

	seat_focus_override_begin(LAB_INPUT_STATE_WINDOW_SWITCHER,
		LAB_CURSOR_DEFAULT);
	update_osd();

	/* Update cursor, in case it is within the area covered by OSD */
	cursor_update_focus();
}

void
osd_cycle(enum lab_cycle_dir direction)
{
	assert(g_server.input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER);

	g_server.osd_state.cycle_view =
		get_next_cycle_view(g_server.osd_state.cycle_view, direction);
	update_osd();
}

void
osd_finish(void)
{
	restore_preview_node();
	seat_focus_override_end();

	g_server.osd_state.preview_node = NULL;
	g_server.osd_state.preview_anchor = NULL;
	g_server.osd_state.cycle_view = NULL;

	destroy_osd_scenes();

	if (g_server.osd_state.preview_outline) {
		/* Destroy the whole multi_rect so we can easily react to new themes */
		wlr_scene_node_destroy(
			&g_server.osd_state.preview_outline->tree->node);
		g_server.osd_state.preview_outline = NULL;
	}

	/* Hiding OSD may need a cursor change */
	cursor_update_focus();
}

static void
preview_cycled_view(struct view *view)
{
	assert(view);
	assert(view->scene_tree);
	struct osd_state *osd_state = &g_server.osd_state;

	/* Move previous selected node back to its original place */
	restore_preview_node();

	/* Store some pointers so we can reset the preview later on */
	osd_state->preview_node = &view->scene_tree->node;
	osd_state->preview_parent = view->scene_tree->node.parent;

	/* Remember the sibling right before the selected node */
	osd_state->preview_anchor = lab_wlr_scene_get_prev_node(
		osd_state->preview_node);
	while (osd_state->preview_anchor && !osd_state->preview_anchor->data) {
		/* Ignore non-view nodes */
		osd_state->preview_anchor = lab_wlr_scene_get_prev_node(
			osd_state->preview_anchor);
	}

	/* Store node enabled / minimized state and force-enable if disabled */
	osd_state->preview_was_enabled = osd_state->preview_node->enabled;
	if (!osd_state->preview_was_enabled) {
		wlr_scene_node_set_enabled(osd_state->preview_node, true);
	}

	/*
	 * FIXME: This abuses an implementation detail of the always-on-top tree.
	 *        Create a permanent g_server.osd_preview_tree instead that can
	 *        also be used as parent for the preview outlines.
	 */
	wlr_scene_node_reparent(osd_state->preview_node,
		g_server.view_tree_always_on_top);

	/* Finally raise selected node to the top */
	wlr_scene_node_raise_to_top(osd_state->preview_node);
}

static void
create_osd_scene(struct output *output, view_list &views)
{
	bool show_workspace = wl_list_length(&rc.workspace_config.workspaces) > 1;
	const char *workspace_name = g_server.workspaces.current->name;

	int w = g_theme.osd_window_switcher_width;
	if (g_theme.osd_window_switcher_width_is_percent) {
		w = output->wlr_output->width
			* g_theme.osd_window_switcher_width / 100;
	}
	int h = views.size() * g_theme.osd_window_switcher_item_height
		+ 2 * g_theme.osd_border_width
		+ 2 * g_theme.osd_window_switcher_padding;
	if (show_workspace) {
		/* workspace indicator */
		h += g_theme.osd_window_switcher_item_height;
	}

	output->osd_scene.tree = wlr_scene_tree_create(output->osd_tree);

	float *text_color = g_theme.osd_label_text_color;
	float *bg_color = g_theme.osd_bg_color;

	/* Draw background */
	struct lab_scene_rect_options bg_opts = {
		.border_colors = (float *[1]) {g_theme.osd_border_color},
		.nr_borders = 1,
		.border_width = g_theme.osd_border_width,
		.bg_color = bg_color,
		.width = w,
		.height = h,
	};
	lab_scene_rect_create(output->osd_scene.tree, &bg_opts);

	int y = g_theme.osd_border_width + g_theme.osd_window_switcher_padding;

	/* Draw workspace indicator */
	if (show_workspace) {
		struct font font = rc.font_osd;
		font.weight = PANGO_WEIGHT_BOLD;

		/* Center workspace indicator on the x axis */
		int x = (w - font_width(&font, workspace_name)) / 2;
		if (x < 0) {
			wlr_log(WLR_ERROR,
				"not enough space for workspace name in osd");
			goto error;
		}

		struct scaled_font_buffer *font_buffer =
			scaled_font_buffer_create(output->osd_scene.tree);
		wlr_scene_node_set_position(&font_buffer->scene_buffer->node, x,
			y + (g_theme.osd_window_switcher_item_height
				- font_height(&font)) / 2);
		scaled_font_buffer_update(font_buffer, workspace_name, 0,
			&font, text_color, bg_color);
		y += g_theme.osd_window_switcher_item_height;
	}

{ /* !goto */
	struct buf buf = BUF_INIT;
	int nr_fields = wl_list_length(&rc.window_switcher.fields);

	/* This is the width of the area available for text fields */
	int field_widths_sum =
		w - 2 * g_theme.osd_border_width
		- 2 * g_theme.osd_window_switcher_padding
		- 2 * g_theme.osd_window_switcher_item_active_border_width
		- (nr_fields + 1) * g_theme.osd_window_switcher_item_padding_x;
	if (field_widths_sum <= 0) {
		wlr_log(WLR_ERROR, "Not enough spaces for osd contents");
		goto error;
	}

	/* Draw text for each node */
	for (auto &view : views) {
		struct osd_scene_item *item =
			wl_array_add(&output->osd_scene.items, sizeof(*item));
		item->view = &view;
		/*
		 *    OSD border
		 * +---------------------------------+
		 * |                                 |
		 * |  item border                    |
		 * |+-------------------------------+|
		 * ||                               ||
		 * ||padding between each field     ||
		 * ||| field-1 | field-2 | field-n |||
		 * ||                               ||
		 * ||                               ||
		 * |+-------------------------------+|
		 * |                                 |
		 * |                                 |
		 * +---------------------------------+
		 */
		int x = g_theme.osd_border_width
			+ g_theme.osd_window_switcher_padding
			+ g_theme.osd_window_switcher_item_active_border_width
			+ g_theme.osd_window_switcher_item_padding_x;
		struct wlr_scene_tree *item_root =
			wlr_scene_tree_create(output->osd_scene.tree);

		struct window_switcher_field *field;
		wl_list_for_each(field, &rc.window_switcher.fields, link) {
			int field_width = field_widths_sum * field->width / 100.0;
			struct wlr_scene_node *node = NULL;
			int height = -1;

			if (field->content == LAB_FIELD_ICON) {
				int icon_size = MIN(field_width,
					g_theme.osd_window_switcher_item_icon_size);
				struct scaled_icon_buffer *icon_buffer =
					scaled_icon_buffer_create(item_root,
						icon_size, icon_size);
				scaled_icon_buffer_set_view(icon_buffer, &view);
				node = &icon_buffer->scene_buffer->node;
				height = icon_size;
			} else {
				buf_clear(&buf);
				osd_field_get_content(field, &buf, &view);

				if (!string_null_or_empty(buf.data)) {
					struct scaled_font_buffer *font_buffer =
						scaled_font_buffer_create(item_root);
					scaled_font_buffer_update(font_buffer,
						buf.data, field_width,
						&rc.font_osd, text_color, bg_color);
					node = &font_buffer->scene_buffer->node;
					height = font_height(&rc.font_osd);
				}
			}

			if (node) {
				int item_height =
					g_theme.osd_window_switcher_item_height;
				wlr_scene_node_set_position(node,
					x, y + (item_height - height) / 2);
			}
			x += field_width
				+ g_theme.osd_window_switcher_item_padding_x;
		}

		/* Highlight around selected window's item */
		int highlight_x = g_theme.osd_border_width
			+ g_theme.osd_window_switcher_padding;
		struct lab_scene_rect_options highlight_opts = {
			.border_colors = (float *[1]) {text_color},
			.nr_borders = 1,
			.border_width =
				g_theme.osd_window_switcher_item_active_border_width,
			.width = w - 2 * g_theme.osd_border_width
				- 2 * g_theme.osd_window_switcher_padding,
			.height = g_theme.osd_window_switcher_item_height,
		};

		struct lab_scene_rect *highlight_rect = lab_scene_rect_create(
			output->osd_scene.tree, &highlight_opts);
		item->highlight_outline = &highlight_rect->tree->node;
		wlr_scene_node_set_position(item->highlight_outline, highlight_x, y);
		wlr_scene_node_set_enabled(item->highlight_outline, false);

		y += g_theme.osd_window_switcher_item_height;
	}
	buf_reset(&buf);

} error:;
	/* Center OSD */
	struct wlr_box usable = output_usable_area_in_layout_coords(output);
	wlr_scene_node_set_position(&output->osd_scene.tree->node,
		usable.x + usable.width / 2 - w / 2,
		usable.y + usable.height / 2 - h / 2);
}

static void
update_item_highlight(struct output *output)
{
	struct osd_scene_item *item;
	wl_array_for_each(item, &output->osd_scene.items) {
		wlr_scene_node_set_enabled(item->highlight_outline,
			item->view == g_server.osd_state.cycle_view);
	}
}

static void
update_osd(void)
{
	auto views = view_list_matching(rc.window_switcher.criteria);

	if (views.empty() || !g_server.osd_state.cycle_view) {
		osd_finish();
		return;
	}

	if (rc.window_switcher.show && g_theme.osd_window_switcher_width > 0) {
		/* Display the actual OSD */
		struct output *output;
		wl_list_for_each(output, &g_server.outputs, link) {
			if (!output_is_usable(output)) {
				continue;
			}
			if (!output->osd_scene.tree) {
				create_osd_scene(output, views);
				assert(output->osd_scene.tree);
			}
			update_item_highlight(output);
		}
	}

	/* Outline current window */
	if (rc.window_switcher.outlines) {
		if (view_is_focusable(g_server.osd_state.cycle_view)) {
			osd_update_preview_outlines(
				g_server.osd_state.cycle_view);
		}
	}

	if (rc.window_switcher.preview) {
		preview_cycled_view(g_server.osd_state.cycle_view);
	}
}
