/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on wlroots/include/types/wlr_buffer.c
 *
 * Copyright (c) 2017, 2018 Drew DeVault
 * Copyright (c) 2018-2021 Simon Ser, Simon Zeni

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef LABWC_BUFFER_H
#define LABWC_BUFFER_H

#include <cairo.h>
#include <wlr/types/wlr_buffer.h>
#include "common/refptr.h"

using u8_array_ptr = std::unique_ptr<uint8_t[]>;

static inline u8_array_ptr
make_u8_array(size_t size)
{
	return std::make_unique<uint8_t[]>(size);
}

struct lab_data_buffer : public wlr_buffer, public refcounted<lab_data_buffer> {
	static const wlr_buffer_impl impl;

	cairo_surface_t *surface = nullptr;
	u8_array_ptr owned_data;
	uint8_t *data = nullptr;
	uint32_t format = 0; // currently always DRM_FORMAT_ARGB8888
	size_t stride = 0;

	// The logical size of the surface in layout pixels.
	// The raw pixel data may be larger or smaller.
	uint32_t logical_width = 0;
	uint32_t logical_height = 0;

	lab_data_buffer(int width, int height);
	~lab_data_buffer();

	void last_unref();
};

/*
 * Create a buffer which holds (and takes ownership of) an existing
 * CAIRO_FORMAT_ARGB32 image surface.
 *
 * The logical size is set to the surface size in pixels, ignoring
 * device scale.
 */
ref<lab_data_buffer> buffer_adopt_cairo_surface(cairo_surface_t *surface);

/*
 * Create a buffer which holds a new CAIRO_FORMAT_ARGB32 image surface.
 * Additionally create a cairo context for drawing to the surface.
 */
ref<lab_data_buffer> buffer_create_cairo(uint32_t logical_width,
	uint32_t logical_height, float scale);

/*
 * Create a buffer which holds (and takes ownership of) raw pixel data
 * in pre-multiplied ARGB32 format.
 *
 * The logical size is set to the width and height of the pixel data.
 */
ref<lab_data_buffer> buffer_create_from_data(u8_array_ptr pixel_data,
	uint32_t width, uint32_t height, uint32_t stride);

/*
 * Create a lab_data_buffer from a wlr_buffer by copying its content.
 * The wlr_buffer must be backed by shm.
 */
refptr<lab_data_buffer> buffer_create_from_wlr_buffer(
	struct wlr_buffer *wlr_buffer);

/*
 * Create a buffer which holds a scaled copy of an existing cairo
 * image surface. The source surface is rendered at the center of the
 * output buffer and shrunk if it overflows from the output buffer.
 */
ref<lab_data_buffer> buffer_scale_cairo_surface(cairo_surface_t *surface,
	int width, int height, double scale);

#endif /* LABWC_BUFFER_H */
