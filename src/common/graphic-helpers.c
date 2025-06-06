// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <cairo.h>
#include <glib.h> /* g_ascii_strcasecmp */
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include "common/graphic-helpers.h"
#include "common/macros.h"
#include "common/mem.h"
#include "xcolor-table.h"

static void
multi_rect_destroy_notify(struct wl_listener *listener, void *data)
{
	struct multi_rect *rect = wl_container_of(listener, rect, destroy);
	wl_list_remove(&rect->destroy.link);
	free(rect);
}

struct multi_rect *
multi_rect_create(struct wlr_scene_tree *parent, float *colors[3], int line_width)
{
	struct multi_rect *rect = znew(*rect);
	rect->line_width = line_width;
	rect->tree = wlr_scene_tree_create(parent);
	rect->destroy.notify = multi_rect_destroy_notify;
	wl_signal_add(&rect->tree->node.events.destroy, &rect->destroy);
	for (size_t i = 0; i < 3; i++) {
		rect->top[i] = wlr_scene_rect_create(rect->tree, 0, 0, colors[i]);
		rect->right[i] = wlr_scene_rect_create(rect->tree, 0, 0, colors[i]);
		rect->bottom[i] = wlr_scene_rect_create(rect->tree, 0, 0, colors[i]);
		rect->left[i] = wlr_scene_rect_create(rect->tree, 0, 0, colors[i]);
		wlr_scene_node_set_position(&rect->top[i]->node,
			i * line_width, i * line_width);
		wlr_scene_node_set_position(&rect->left[i]->node,
			i * line_width, (i + 1) * line_width);
	}
	return rect;
}

void
multi_rect_set_size(struct multi_rect *rect, int width, int height)
{
	assert(rect);
	int line_width = rect->line_width;

	/*
	 * The outmost outline is drawn like below:
	 *
	 * |--width--|
	 *
	 * +---------+  ---
	 * +-+-----+-+   |
	 * | |     | | height
	 * | |     | |   |
	 * +-+-----+-+   |
	 * +---------+  ---
	 */
	for (int i = 0; i < 3; i++) {
		/* Reposition, top and left don't ever change */
		wlr_scene_node_set_position(&rect->right[i]->node,
			width - (i + 1) * line_width, (i + 1) * line_width);
		wlr_scene_node_set_position(&rect->bottom[i]->node,
			i * line_width, height - (i + 1) * line_width);

		/* Update sizes */
		wlr_scene_rect_set_size(rect->top[i],
			MAX(width - i * line_width * 2, 0),
			line_width);
		wlr_scene_rect_set_size(rect->bottom[i],
			MAX(width - i * line_width * 2, 0),
			line_width);
		wlr_scene_rect_set_size(rect->left[i],
			line_width,
			MAX(height - (i + 1) * line_width * 2, 0));
		wlr_scene_rect_set_size(rect->right[i],
			line_width,
			MAX(height - (i + 1) * line_width * 2, 0));
	}
}

/* Draws a border with a specified line width */
void
draw_cairo_border(cairo_t *cairo, struct wlr_fbox fbox, double line_width)
{
	cairo_save(cairo);

	/* The anchor point of a line is in the center */
	fbox.x += line_width / 2.0;
	fbox.y += line_width / 2.0;
	fbox.width -= line_width;
	fbox.height -= line_width;
	cairo_set_line_width(cairo, line_width);
	cairo_rectangle(cairo, fbox.x, fbox.y, fbox.width, fbox.height);
	cairo_stroke(cairo);

	cairo_restore(cairo);
}

/* Sets the cairo color. Splits the single color channels */
void
set_cairo_color(cairo_t *cairo, const float *c)
{
	/*
	 * We are dealing with pre-multiplied colors
	 * but cairo expects unmultiplied colors here
	 */
	float alpha = c[3];

	if (alpha == 0.0f) {
		cairo_set_source_rgba(cairo, 0, 0, 0, 0);
		return;
	}

	cairo_set_source_rgba(cairo, c[0] / alpha, c[1] / alpha,
		c[2] / alpha, alpha);
}

cairo_pattern_t *
color_to_pattern(const float *c)
{
	float alpha = c[3];

	if (alpha == 0.0f) {
		return cairo_pattern_create_rgba(0, 0, 0, 0);
	}

	return cairo_pattern_create_rgba(
		c[0] / alpha, c[1] / alpha, c[2] / alpha, alpha);
}

/*
 * This is used as an optimization in font rendering and errs on the
 * side of returning false (not opaque) for unknown pattern types.
 *
 * The 0.999 alpha threshold was chosen to be greater than 254/255
 * (about 0.996) while leaving some margin for rounding errors.
 */
bool
is_pattern_opaque(cairo_pattern_t *pattern)
{
	double alpha = 0;
	int stops = 0;

	/* solid color? */
	if (cairo_pattern_get_rgba(pattern, NULL, NULL, NULL, &alpha)
			== CAIRO_STATUS_SUCCESS) {
		return (alpha >= 0.999);
	}

	/* gradient? */
	if (cairo_pattern_get_color_stop_count(pattern, &stops)
			== CAIRO_STATUS_SUCCESS) {
		for (int s = 0; s < stops; s++) {
			cairo_pattern_get_color_stop_rgba(pattern, s,
				NULL, NULL, NULL, NULL, &alpha);
			if (alpha < 0.999) {
				return false;
			}
		}
		return true;
	}

	return false; /* unknown pattern type */
}

static int
compare_xcolor_entry(const void *a, const void *b)
{
	/* using ASCII version to avoid locale-dependent ordering */
	return g_ascii_strcasecmp((const char *)a,
		color_names + ((const struct xcolor_entry *)b)->name_offset);
}

bool
lookup_named_color(const char *name, uint32_t *argb)
{
	struct xcolor_entry *found = bsearch(name, xcolors, ARRAY_SIZE(xcolors),
		sizeof(struct xcolor_entry), compare_xcolor_entry);
	if (!found) {
		return false;
	}

	*argb = 0xFF000000u | ((uint32_t)found->red << 16)
		| ((uint32_t)found->green << 8) | found->blue;
	return true;
}
