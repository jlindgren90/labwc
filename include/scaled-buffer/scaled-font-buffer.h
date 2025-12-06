/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_FONT_BUFFER_H
#define LABWC_SCALED_FONT_BUFFER_H

#include "common/font.h"
#include "scaled-buffer.h"

using cairo_pattern_ptr = ownptr<cairo_pattern_t, cairo_pattern_destroy>;

/**
 * Auto scaling font buffer, providing a wlr_scene_buffer node for display.
 * To actually show some text, scaled_font_buffer_update() has to be called.
 */
struct scaled_font_buffer : public scaled_buffer {
	int width;  // unscaled, read only
	int height; // unscaled, read only

	lab_str text;
	int max_width = 0;
	float color[4] = {0};
	float bg_color[4] = {0};
	struct font font = {};

	// The following fields are used only for the titlebar, where
	// the font buffer can be rendered with a pattern background to
	// support gradients. In this case, the font buffer is also
	// padded to a fixed height (with the text centered vertically)
	// in order to align the pattern with the rest of the titlebar.
	int fixed_height = 0;         // logical pixels
	cairo_pattern_ptr bg_pattern; // overrides bg_color if set

	// Takes a new reference to bg_pattern (if set)
	scaled_font_buffer(wlr_scene_tree *parent, int fixed_height = 0,
			cairo_pattern_t *bg_pattern = nullptr)
		: scaled_buffer(SCALED_FONT_BUFFER, parent),
			fixed_height(fixed_height),
			bg_pattern(bg_pattern
				? cairo_pattern_reference(bg_pattern)
				: nullptr) {}

	refptr<lab_data_buffer> create_buffer(double scale) override;
	bool equal(scaled_buffer &other) override;
};

/**
 * Update an existing auto scaling font buffer.
 *
 * No steps are taken to detect if its actually required to render a new buffer.
 * This should be done by the caller to prevent useless recreation of the same
 * buffer in case nothing actually changed.
 *
 * Some basic checks could be something like
 * - truncated = buffer->width == max_width
 * - text_changed = strcmp(old_text, new_text)
 * - font and color the same
 *
 * bg_color is ignored for font buffers created with
 * scaled_font_buffer_create_for_titlebar().
 */
void scaled_font_buffer_update(struct scaled_font_buffer *self, const char *text,
	int max_width, struct font *font, const float *color,
	const float *bg_color);

#endif /* LABWC_SCALED_FONT_BUFFER_H */
