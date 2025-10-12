/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_IMG_H
#define LABWC_IMG_H

#include <vector>
#include "buffer.h"
#include "config.h"

#if HAVE_RSVG
typedef struct _RsvgHandle RsvgHandle;
#endif

enum lab_img_type {
	LAB_IMG_PNG,
	LAB_IMG_SVG,
	LAB_IMG_XBM,
	LAB_IMG_XPM,
};

struct lab_img_data : public ref_owned<lab_img_data> {
	refptr<lab_data_buffer> buffer; // for PNG/XBM/XPM image
#if HAVE_RSVG
	RsvgHandle *svg; // for SVG image
#endif

	~lab_img_data();
};

typedef void (*lab_img_modifier_func_t)(cairo_t *cairo, int w, int h);

struct lab_img {
	// Shared internal image cache
	refptr<lab_img_data> data;

	// "Modifiers" are functions that perform some additional
	// drawing operation after the image is rendered on a buffer
	// with lab_img_render(). For example, hover effects for window
	// buttons can be drawn over the rendered image.
	std::vector<lab_img_modifier_func_t> modifiers;

	lab_img() {}

	bool valid() const { return (bool)data; }

	bool operator==(const lab_img &other) const {
		return data == other.data && modifiers == other.modifiers;
	}
	bool operator!=(const lab_img &other) const {
		return data != other.data || modifiers != other.modifiers;
	}

	refptr<lab_data_buffer> render(int width, int height, double scale);

	static lab_img load(lab_img_type type, const char *path,
		float *xbm_color);

	// Creates button from monochrome bitmap
	//   @bitmap: bitmap data array in hexadecimal xbm format
	//   @rgba: color
	//
	// Example bitmap: { 0x3f, 0x3f, 0x21, 0x21, 0x21, 0x3f }
	static lab_img load_from_bitmap(const char *bitmap, float *rgba);

private:
	explicit lab_img(refptr<lab_data_buffer> buffer)
		: data(buffer ? new lab_img_data{.buffer = buffer} : nullptr) {}
#if HAVE_RSVG
	explicit lab_img(RsvgHandle *svg)
		: data(svg ? new lab_img_data{.svg = svg} : nullptr) {}
#endif
};

#endif /* LABWC_IMG_H */
