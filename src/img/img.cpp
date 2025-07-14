// SPDX-License-Identifier: GPL-2.0-only

#include "img/img.h"
#include "common/string-helpers.h"
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
		return lab_img();
	}

	switch (type) {
	case LAB_IMG_PNG:
		return lab_img(img_png_load(path));
	case LAB_IMG_XBM:
		assert(xbm_color);
		return lab_img(img_xbm_load(path, xbm_color));
	case LAB_IMG_XPM:
		return lab_img(img_xpm_load(path));
	case LAB_IMG_SVG:
#if HAVE_RSVG
		return lab_img(img_svg_load(path));
#endif
		break;
	}

	return lab_img();
}

lab_img
lab_img::load_from_bitmap(const char *bitmap, float *rgba)
{
	return lab_img(img_xbm_load_from_bitmap(bitmap, rgba));
}

refptr<lab_data_buffer>
lab_img::render(int width, int height, double scale)
{
	refptr<lab_data_buffer> buffer;

	CHECK_PTR_OR_RET_VAL(this->data, data, buffer);

	/* Render the image into the buffer for the given size */
	if (CHECK_PTR(data->buffer, orig)) {
		buffer = buffer_scale_cairo_surface(orig->surface, width,
			height, scale);
#if HAVE_RSVG
	} else if (data->svg) {
		buffer = img_svg_render(data->svg, width, height, scale);
#endif
	}

	if (CHECK_PTR(buffer, buf)) {
		/* Apply modifiers to the buffer (e.g. draw hover overlay) */
		cairo_t *cairo = cairo_create(buf->surface);
		for (auto modifier : modifiers) {
			cairo_save(cairo);
			(*modifier)(cairo, width, height);
			cairo_restore(cairo);
		}

		cairo_surface_flush(buf->surface);
		cairo_destroy(cairo);
	}

	return buffer;
}
