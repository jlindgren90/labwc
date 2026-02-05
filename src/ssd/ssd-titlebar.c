// SPDX-License-Identifier: GPL-2.0-only

#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include <wlr/render/pixman.h>
#include <wlr/types/wlr_scene.h>
#include "buffer.h"
#include "common/mem.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "scaled-buffer/scaled-img-buffer.h"
#include "ssd.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"

static void set_alt_maximize_icon(struct ssd *ssd, bool enable);
static void update_visible_buttons(struct ssd *ssd);

void
ssd_titlebar_create(struct ssd *ssd, struct wlr_buffer *icon_buffer)
{
	struct view *view = ssd->view;
	int width = view->st->current.width;
	bool maximized = view->st->maximized == VIEW_AXIS_BOTH;

	ssd->titlebar.tree = wlr_scene_tree_create(ssd->tree);
	node_descriptor_create(&ssd->titlebar.tree->node,
		LAB_NODE_TITLEBAR, view, /*data*/ NULL);

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		subtree->tree = wlr_scene_tree_create(ssd->titlebar.tree);
		struct wlr_scene_tree *parent = subtree->tree;
		wlr_scene_node_set_enabled(&parent->node, active);
		wlr_scene_node_set_position(&parent->node, 0, -g_theme.titlebar_height);

		struct wlr_buffer *titlebar_fill =
			&g_theme.window[active].titlebar_fill->base;

		/* Background */
		subtree->bar = wlr_scene_buffer_create(parent, titlebar_fill);
		/*
		 * Work around the wlroots/pixman bug that widened 1px buffer
		 * becomes translucent when bilinear filtering is used.
		 * TODO: remove once https://gitlab.freedesktop.org/wlroots/wlroots/-/issues/3990
		 * is solved
		 */
		if (wlr_renderer_is_pixman(g_server.renderer)) {
			wlr_scene_buffer_set_filter_mode(
				subtree->bar, WLR_SCALE_FILTER_NEAREST);
		}
		int overlap = maximized ? 0 : BORDER_PX_SIDE - 2;
		wlr_scene_node_set_position(&subtree->bar->node, -overlap, 0);
		wlr_scene_buffer_set_dest_size(subtree->bar,
			width + 2 * overlap, g_theme.titlebar_height);

		/* Title */
		subtree->title = scaled_font_buffer_create_for_titlebar(
			subtree->tree, g_theme.titlebar_height,
			g_theme.window[active].titlebar_pattern);
		assert(subtree->title);
		node_descriptor_create(&subtree->title->scene_buffer->node,
			LAB_NODE_TITLE, view, /*data*/ NULL);

		/* Buttons */
		int x = g_theme.window_titlebar_padding_width;

		/* Center vertically within titlebar */
		int y = (g_theme.titlebar_height - g_theme.window_button_height) / 2;

		subtree->button_left = attach_ssd_button(
			LAB_NODE_BUTTON_WINDOW_ICON, parent, NULL, x, y, view);

		x = width - g_theme.window_titlebar_padding_width + g_theme.window_button_spacing;

		static const enum lab_node_type types[NR_TITLE_BUTTONS_RIGHT] = {
			LAB_NODE_BUTTON_ICONIFY,
			LAB_NODE_BUTTON_MAXIMIZE,
			LAB_NODE_BUTTON_CLOSE,
		};

		for (int b = NR_TITLE_BUTTONS_RIGHT - 1; b >= 0; b--) {
			x -= g_theme.window_button_width + g_theme.window_button_spacing;
			enum lab_node_type type = types[b];
			struct lab_img **imgs =
				g_theme.window[active].button_imgs[type];
			subtree->buttons_right[b] = attach_ssd_button(type,
				parent, imgs, x, y, view);
		}
	}

	update_visible_buttons(ssd);

	ssd_update_icon(ssd, icon_buffer);
	ssd_update_title(ssd);

	if (maximized) {
		set_alt_maximize_icon(ssd, true);
		ssd->state.was_maximized = true;
	}
}

static void
update_button_state(struct ssd_button *button, enum lab_button_state state,
		bool enable)
{
	if (enable) {
		button->state_set |= state;
	} else {
		button->state_set &= ~state;
	}
	/* Switch the displayed icon buffer to the new one */
	for (uint8_t state_set = LAB_BS_DEFAULT;
			state_set <= LAB_BS_ALL; state_set++) {
		struct scaled_img_buffer *buffer = button->img_buffers[state_set];
		if (!buffer) {
			continue;
		}
		wlr_scene_node_set_enabled(&buffer->scene_buffer->node,
			state_set == button->state_set);
	}
}

static void
set_alt_maximize_icon(struct ssd *ssd, bool enable)
{
	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		// Maximize is always middle right-hand button
		struct ssd_button *button = subtree->buttons_right[1];
		update_button_state(button, LAB_BS_TOGGLED, enable);
	}
}

/*
 * Usually this function just enables all the nodes for buttons, but some
 * buttons can be hidden for small windows (e.g. xterm -geometry 1x1).
 */
static void
update_visible_buttons(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int width = MAX(view->st->current.width
		- 2 * g_theme.window_titlebar_padding_width, 0);
	int button_width = g_theme.window_button_width;
	int button_spacing = g_theme.window_button_spacing;
	int button_count_left = 1; // menu/window icon
	int button_count_right = NR_TITLE_BUTTONS_RIGHT;

	/* Make sure infinite loop never occurs */
	assert(button_width > 0);

	/*
	 * The corner-left button is lastly removed as it's usually a window
	 * menu button (or an app icon button in the future).
	 *
	 * There is spacing to the inside of each button, including between the
	 * innermost buttons and the window title. See also get_title_offsets().
	 */
	while (width < ((button_width + button_spacing)
			* (button_count_left + button_count_right))) {
		if (button_count_left > button_count_right) {
			button_count_left--;
		} else {
			button_count_right--;
		}
	}

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];

		wlr_scene_node_set_enabled(subtree->button_left->node,
			button_count_left > 0);

		int button_count = 0;
		for (int b = NR_TITLE_BUTTONS_RIGHT - 1; b >= 0; b--) {
			wlr_scene_node_set_enabled(
				subtree->buttons_right[b]->node,
				button_count < button_count_right);
			button_count++;
		}
	}
}

void
ssd_titlebar_update(struct ssd *ssd)
{
	struct view *view = ssd->view;
	int width = view->st->current.width;

	bool maximized = view->st->maximized == VIEW_AXIS_BOTH;

	if (ssd->state.was_maximized != maximized) {
		set_alt_maximize_icon(ssd, maximized);
	}

	if (ssd->state.was_maximized == maximized
			&& ssd->state.geometry.width == width) {
		return;
	}
	ssd->state.was_maximized = maximized;

	update_visible_buttons(ssd);

	/* Center buttons vertically within titlebar */
	int y = (g_theme.titlebar_height - g_theme.window_button_height) / 2;
	int x;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		int overlap = maximized ? 0 : BORDER_PX_SIDE - 2;
		wlr_scene_node_set_position(&subtree->bar->node, -overlap, 0);
		wlr_scene_buffer_set_dest_size(subtree->bar,
			width + 2 * overlap, g_theme.titlebar_height);

		x = g_theme.window_titlebar_padding_width;
		wlr_scene_node_set_position(subtree->button_left->node, x, y);

		x = width - g_theme.window_titlebar_padding_width + g_theme.window_button_spacing;
		for (int b = NR_TITLE_BUTTONS_RIGHT - 1; b >= 0; b--) {
			x -= g_theme.window_button_width + g_theme.window_button_spacing;
			wlr_scene_node_set_position(
				subtree->buttons_right[b]->node, x, y);
		}
	}

	ssd_update_title(ssd);
}

void
ssd_titlebar_destroy(struct ssd *ssd)
{
	if (!ssd->titlebar.tree) {
		return;
	}

	zfree(ssd->state.title.text);
	wlr_scene_node_destroy(&ssd->titlebar.tree->node);
	ssd->titlebar = (struct ssd_titlebar_scene){0};
}

/*
 * For ssd_update_title* we do not early out because
 * .active and .inactive may result in different sizes
 * of the title (font family/size) or background of
 * the title (different button/border width).
 *
 * Both, wlr_scene_node_set_enabled() and wlr_scene_node_set_position()
 * check for actual changes and return early if there is no change in state.
 * Always using wlr_scene_node_set_enabled(node, true) will thus not cause
 * any unnecessary screen damage and makes the code easier to follow.
 */

static void
ssd_update_title_positions(struct ssd *ssd, int offset_left, int offset_right)
{
	struct view *view = ssd->view;
	int width = view->st->current.width;
	int title_bg_width = width - offset_left - offset_right;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		struct scaled_font_buffer *title = subtree->title;
		int x, y;

		x = offset_left;
		y = (g_theme.titlebar_height - title->height) / 2;

		if (title_bg_width <= 0) {
			wlr_scene_node_set_enabled(&title->scene_buffer->node, false);
			continue;
		}
		wlr_scene_node_set_enabled(&title->scene_buffer->node, true);

		if (g_theme.window_label_text_justify == LAB_JUSTIFY_CENTER) {
			if (title->width + MAX(offset_left, offset_right) * 2 <= width) {
				/* Center based on the full width */
				x = (width - title->width) / 2;
			} else {
				/*
				 * Center based on the width between the buttons.
				 * Title jumps around once this is hit but its still
				 * better than to hide behind the buttons on the right.
				 */
				x += (title_bg_width - title->width) / 2;
			}
		} else if (g_theme.window_label_text_justify == LAB_JUSTIFY_RIGHT) {
			x += title_bg_width - title->width;
		} else if (g_theme.window_label_text_justify == LAB_JUSTIFY_LEFT) {
			/* TODO: maybe add some theme x padding here? */
		}
		wlr_scene_node_set_position(&title->scene_buffer->node, x, y);
	}
}

/*
 * Get left/right offsets of the title area based on visible/hidden states of
 * buttons set in update_visible_buttons().
 */
static void
get_title_offsets(struct ssd *ssd, int *offset_left, int *offset_right)
{
	struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[SSD_ACTIVE];
	int button_width = g_theme.window_button_width;
	int button_spacing = g_theme.window_button_spacing;
	int padding_width = g_theme.window_titlebar_padding_width;
	*offset_left = padding_width;
	*offset_right = padding_width;

	if (subtree->button_left->node->enabled) {
		*offset_left += button_width + button_spacing;
	}

	for (int b = 0; b < NR_TITLE_BUTTONS_RIGHT; b++) {
		if (subtree->buttons_right[b]->node->enabled) {
			*offset_right += button_width + button_spacing;
		}
	}
}

void
ssd_update_title(struct ssd *ssd)
{
	if (!ssd) {
		return;
	}

	struct view *view = ssd->view;
	if (string_null_or_empty(view->st->title)) {
		return;
	}

	struct ssd_state_title *state = &ssd->state.title;
	bool title_unchanged =
		state->text && !strcmp(view->st->title, state->text);

	int offset_left, offset_right;
	get_title_offsets(ssd, &offset_left, &offset_right);
	int title_bg_width = view->st->current.width - offset_left - offset_right;

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree = &ssd->titlebar.subtrees[active];
		struct ssd_state_title_width *dstate = &state->dstates[active];
		const float *text_color = g_theme.window[active].label_text_color;
		struct font *font = active ?
			&rc.font_activewindow : &rc.font_inactivewindow;

		if (title_bg_width <= 0) {
			dstate->truncated = true;
			continue;
		}

		if (title_unchanged
				&& !dstate->truncated && dstate->width < title_bg_width) {
			/* title the same + we don't need to resize title */
			continue;
		}

		const float bg_color[4] = {0, 0, 0, 0}; /* ignored */
		scaled_font_buffer_update(subtree->title, view->st->title,
			title_bg_width, font, text_color, bg_color);

		/* And finally update the cache */
		dstate->width = subtree->title->width;
		dstate->truncated = title_bg_width <= dstate->width;
	}

	if (!title_unchanged) {
		xstrdup_replace(state->text, view->st->title);
	}
	ssd_update_title_positions(ssd, offset_left, offset_right);
}

int
ssd_get_icon_buffer_size(void)
{
	return g_theme.window_icon_size * g_server.max_output_scale;
}

void
ssd_update_icon(struct ssd *ssd, struct wlr_buffer *icon_buffer)
{
	if (!ssd) {
		return;
	}

	enum ssd_active_state active;
	FOR_EACH_ACTIVE_STATE(active) {
		struct ssd_titlebar_subtree *subtree =
			&ssd->titlebar.subtrees[active];
		if (subtree->button_left->window_icon) {
			wlr_scene_buffer_set_buffer(
				subtree->button_left->window_icon, icon_buffer);
		}
	}
}

void
ssd_update_hovered_button(struct wlr_scene_node *node)
{
	struct ssd_button *button = NULL;

	if (node && node->data) {
		button = node_try_ssd_button_from_node(node);
		if (button == g_server.hovered_button) {
			/* Cursor is still on the same button */
			return;
		}
	}

	/* Disable old hover */
	if (g_server.hovered_button) {
		update_button_state(g_server.hovered_button, LAB_BS_HOVERED, false);
	}
	g_server.hovered_button = button;
	if (button) {
		update_button_state(button, LAB_BS_HOVERED, true);
	}
}
