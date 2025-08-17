// SPDX-License-Identifier: GPL-2.0-only
#include "common/scaled-font-buffer.h"
#include <wlr/util/log.h>

refptr<lab_data_buffer>
scaled_font_buffer::create_buffer(double scale)
{
	cairo_pattern_t *bg_pattern = this->bg_pattern.get();
	cairo_pattern_ptr solid_bg_pattern;

	if (!bg_pattern) {
		solid_bg_pattern.reset(color_to_pattern(this->bg_color));
		bg_pattern = solid_bg_pattern.get();
	}

	/* Buffer gets free'd automatically along the backing wlr_buffer */
	auto buffer = font_buffer_create(this->max_width, this->height,
		this->text.c(), &this->font, this->color, bg_pattern, scale);

	if (!buffer) {
		wlr_log(WLR_ERROR, "font_buffer_create() failed");
	}

	return buffer;
}

bool
scaled_font_buffer::equal(scaled_scene_buffer &other)
{
	if (other.type != SCALED_FONT_BUFFER) {
		return false;
	}

	auto a = this;
	auto b = static_cast<scaled_font_buffer *>(&other);

	return a->text == b->text
		&& a->max_width == b->max_width
		&& a->font.name == b->font.name
		&& a->font.size == b->font.size
		&& a->font.slant == b->font.slant
		&& a->font.weight == b->font.weight
		&& !memcmp(a->color, b->color, sizeof(a->color))
		&& !memcmp(a->bg_color, b->bg_color, sizeof(a->bg_color))
		&& a->fixed_height == b->fixed_height
		&& a->bg_pattern == b->bg_pattern;
}

/* Public API */
void
scaled_font_buffer_update(struct scaled_font_buffer *self, const char *text,
		int max_width, struct font *font, const float *color,
		const float *bg_color)
{
	assert(self);
	assert(text);
	assert(font);
	assert(color);

	/* Update internal state */
	self->text = lab_str(text);
	self->max_width = max_width;
	self->font = *font;
	memcpy(self->color, color, sizeof(self->color));
	memcpy(self->bg_color, bg_color, sizeof(self->bg_color));

	/* Calculate the size of font buffer and request re-rendering */
	int computed_height;
	font_get_buffer_size(self->max_width, self->text.c(), &self->font,
		&self->width, &computed_height);
	self->height = (self->fixed_height > 0) ?
		self->fixed_height : computed_height;
	scaled_scene_buffer_request_update(self, self->width, self->height);
}

void
scaled_font_buffer_set_max_width(struct scaled_font_buffer *self, int max_width)
{
	self->max_width = max_width;

	int computed_height;
	font_get_buffer_size(self->max_width, self->text.c(), &self->font,
		&self->width, &computed_height);
	self->height = (self->fixed_height > 0) ?
		self->fixed_height : computed_height;
	scaled_scene_buffer_request_update(self, self->width, self->height);
}
