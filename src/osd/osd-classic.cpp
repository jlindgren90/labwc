// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "common/buf.h"
#include "common/font.h"
#include "common/lab-scene-rect.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "osd.h"
#include "output.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "theme.h"
#include "view.h"
#include "workspaces.h"

struct osd_classic_scene_item {
	struct view *view;
	struct wlr_scene_node *highlight_outline;
};

static void
osd_classic_create(struct output *output, reflist<view> &views)
{
	assert(!output->osd_scene.tree);

	struct window_switcher_classic_theme *switcher_theme =
		&g_theme.osd_window_switcher_classic;
	bool show_workspace = rc.workspace_config.names.size() > 1;

	int w = switcher_theme->width;
	if (switcher_theme->width_is_percent) {
		w = output->wlr_output->width * switcher_theme->width / 100;
	}
	int h = views.size() * switcher_theme->item_height
		+ 2 * g_theme.osd_border_width + 2 * switcher_theme->padding;
	if (show_workspace) {
		/* workspace indicator */
		h += switcher_theme->item_height;
	}

	output->osd_scene.tree = wlr_scene_tree_create(output->osd_tree);

	float *bg_color = g_theme.osd_bg_color;
	float *border_color = g_theme.osd_border_color;
	float *text_color = g_theme.osd_label_text_color;

	/* Draw background */
	struct lab_scene_rect_options bg_opts = {
		.border_colors = &border_color,
		.nr_borders = 1,
		.border_width = g_theme.osd_border_width,
		.bg_color = bg_color,
		.width = w,
		.height = h,
	};
	lab_scene_rect_create(output->osd_scene.tree, &bg_opts);

	int y = g_theme.osd_border_width + switcher_theme->padding;

	/* Draw workspace indicator */
	if (show_workspace) {
		struct font font = rc.font_osd;
		font.weight = PANGO_WEIGHT_BOLD;

		/* Center workspace indicator on the x axis */
		ASSERT_PTR(g_server.workspaces.current, current);
		int x = (w - font_width(&font, current->name.c())) / 2;
		if (x < 0) {
			wlr_log(WLR_ERROR,
				"not enough space for workspace name in osd");
			goto error;
		}

		struct scaled_font_buffer *font_buffer =
			new scaled_font_buffer(output->osd_scene.tree);
		wlr_scene_node_set_position(&font_buffer->scene_buffer->node,
			x, y + (switcher_theme->item_height - font_height(&font)) / 2);
		scaled_font_buffer_update(font_buffer, current->name.c(), 0,
			&font, text_color, bg_color);
		y += switcher_theme->item_height;
	}

{ /* !goto */
	struct buf buf = BUF_INIT;
	int nr_fields = rc.window_switcher.fields.size();

	/* This is the width of the area available for text fields */
	int field_widths_sum = w - 2 * g_theme.osd_border_width
		- 2 * switcher_theme->padding
		- 2 * switcher_theme->item_active_border_width
		- (nr_fields + 1) * switcher_theme->item_padding_x;
	if (field_widths_sum <= 0) {
		wlr_log(WLR_ERROR, "Not enough spaces for osd contents");
		goto error;
	}

{ /* !goto */
	/* Draw text for each node */
	for (auto &view : views) {
		struct osd_classic_scene_item *item =
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
			+ switcher_theme->padding
			+ switcher_theme->item_active_border_width
			+ switcher_theme->item_padding_x;
		struct wlr_scene_tree *item_root =
			wlr_scene_tree_create(output->osd_scene.tree);

		for (auto &field : rc.window_switcher.fields) {
			int field_width = field_widths_sum * field.width / 100.0;
			struct wlr_scene_node *node = NULL;
			int height = -1;

			if (field.content == LAB_FIELD_ICON) {
				int icon_size = MIN(field_width,
					switcher_theme->item_icon_size);
				struct scaled_icon_buffer *icon_buffer =
					new scaled_icon_buffer(item_root,
						icon_size, icon_size);
				scaled_icon_buffer_set_view(icon_buffer, &view);
				node = &icon_buffer->scene_buffer->node;
				height = icon_size;
			} else {
				buf_clear(&buf);
				osd_field_get_content(&field, &buf, &view);

				if (!string_null_or_empty(buf.data)) {
					struct scaled_font_buffer *font_buffer =
						new scaled_font_buffer(item_root);
					scaled_font_buffer_update(font_buffer,
						buf.data, field_width,
						&rc.font_osd, text_color, bg_color);
					node = &font_buffer->scene_buffer->node;
					height = font_height(&rc.font_osd);
				}
			}

			if (node) {
				int item_height = switcher_theme->item_height;
				wlr_scene_node_set_position(node,
					x, y + (item_height - height) / 2);
			}
			x += field_width + switcher_theme->item_padding_x;
		}

		/* Highlight around selected window's item */
		int highlight_x = g_theme.osd_border_width
				+ switcher_theme->padding;
		struct lab_scene_rect_options highlight_opts = {
			.border_colors = &text_color,
			.nr_borders = 1,
			.border_width = switcher_theme->item_active_border_width,
			.width = w - 2 * g_theme.osd_border_width
				- 2 * switcher_theme->padding,
			.height = switcher_theme->item_height,
		};

		struct lab_scene_rect *highlight_rect = lab_scene_rect_create(
			output->osd_scene.tree, &highlight_opts);
		item->highlight_outline = &highlight_rect->tree->node;
		wlr_scene_node_set_position(item->highlight_outline, highlight_x, y);
		wlr_scene_node_set_enabled(item->highlight_outline, false);

		y += switcher_theme->item_height;
	}
	buf_reset(&buf);

} } error:;
	/* Center OSD */
	struct wlr_box usable = output_usable_area_in_layout_coords(output);
	wlr_scene_node_set_position(&output->osd_scene.tree->node,
		usable.x + usable.width / 2 - w / 2,
		usable.y + usable.height / 2 - h / 2);
}

static void
osd_classic_update(struct output *output)
{
	struct osd_classic_scene_item *item;
	wl_array_for_each(item, &output->osd_scene.items) {
		wlr_scene_node_set_enabled(item->highlight_outline,
			item->view == g_server.osd_state.cycle_view);
	}
}

struct osd_impl osd_classic_impl = {
	.create = osd_classic_create,
	.update = osd_classic_update,
};
