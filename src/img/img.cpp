// SPDX-License-Identifier: GPL-2.0-only

#include "common/string-helpers.h"
#include "img/img.h"
#include "img/img-png.h"
#if HAVE_RSVG
#include "img/img-svg.h"
#endif
#include "img/img-xbm.h"
#include "img/img-xpm.h"

lab_img_data::~lab_img_data()
{
#if HAVE_RSVG
	if (svg) {
		g_object_unref(svg);
	}
#endif
}

lab_img
lab_img::load(enum lab_img_type type, const char *path, float *xbm_color)
{
	if (string_null_or_empty(path)) {
		return {};
	}

	refptr img_data{new lab_img_data{}};
	img_data->type = type;

	switch (type) {
	case LAB_IMG_PNG:
		img_data->buffer = img_png_load(path);
		break;
	case LAB_IMG_XBM:
		assert(xbm_color);
		img_data->buffer = img_xbm_load(path, xbm_color);
		break;
	case LAB_IMG_XPM:
		img_data->buffer = img_xpm_load(path);
		break;
	case LAB_IMG_SVG:
#if HAVE_RSVG
		img_data->svg = img_svg_load(path);
#endif
		break;
	}

	bool img_is_loaded = (bool)img_data->buffer;
#if HAVE_RSVG
	img_is_loaded |= (bool)img_data->svg;
#endif

	if (img_is_loaded) {
		return {img_data};
	} else {
		return {};
	}
}

lab_img
lab_img::load_from_bitmap(const char *bitmap, float *rgba)
{
	auto buffer = img_xbm_load_from_bitmap(bitmap, rgba);
	if (!buffer) {
		return {};
	}

	refptr img_data{new lab_img_data{}};
	img_data->type = LAB_IMG_XBM;
	img_data->buffer = buffer;

	return {img_data};
}

lab_data_buffer_ptr
lab_img::render(int width, int height, double scale)
{
	assert(valid());

	lab_data_buffer_ptr buffer;

	/* Render the image into the buffer for the given size */
	switch (this->data->type) {
	case LAB_IMG_PNG:
	case LAB_IMG_XBM:
	case LAB_IMG_XPM:
		buffer = buffer_scale_cairo_surface(this->data->buffer->surface,
			width, height, scale);
		break;
#if HAVE_RSVG
	case LAB_IMG_SVG:
		buffer = img_svg_render(this->data->svg, width, height, scale);
		break;
#endif
	default:
		break;
	}

	if (!buffer) {
		return {};
	}

	/* Apply modifiers to the buffer (e.g. draw hover overlay) */
	cairo_t *cairo = cairo_create(buffer->surface);
	for (auto modifier : this->modifiers) {
		cairo_save(cairo);
		(*modifier)(cairo, width, height);
		cairo_restore(cairo);
	}

	cairo_surface_flush(buffer->surface);
	cairo_destroy(cairo);

	return buffer;
}
