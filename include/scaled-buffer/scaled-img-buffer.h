/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_IMG_BUFFER_H
#define LABWC_SCALED_IMG_BUFFER_H

#include "scaled-buffer.h"

struct wlr_scene_node;
struct lab_img;

/*
 * Auto scaling image buffer, providing a wlr_scene_buffer node for display.
 * The constructor clones the lab_img passed as the image source, so callers are
 * free to destroy it.
 */
struct scaled_img_buffer : public scaled_buffer {
	lab_img *img = nullptr;
	int width = 0;
	int height = 0;

	scaled_img_buffer(wlr_scene_tree *parent, lab_img *img, int width,
		int height);
	~scaled_img_buffer();

	refptr<lab_data_buffer> create_buffer(double scale) override;
	bool equal(scaled_buffer &other) override;
};

/*
 *                                                 |                 |
 *                                       .------------------.  .------------.
 *                   scaled_img_buffer   | new_output_scale |  | set_buffer |
 *                     architecture      ´------------------`  ´------------`
 *                                                 |                ^
 *                .--------------------------------|----------------|-------------.
 *                |                                v                |             |
 *                |  .-------------------.    .-------------------------.         |
 *                |  | scaled_img_buffer |----| wlr_buffer LRU cache(2) |<----,   |
 *                |  ´-------------------`    ´-------------------------`     |   |
 *                |            |                           |                  |   |
 *                |            |               .--------------------------.   |   |
 *                |            |               | wlr_buffer LRU cache of  |   |   |
 *   .-------.    |            |               | other scaled_img_buffers |   |   |
 *   | theme |    |            |               |   with lab_img_equal()   |   |   |
 *   ´-------`    |            |               ´--------------------------`   |   |
 *       |        |            |                  /              |            |   |
 *       |        |            |             not found         found          |   |
 *  .---------.   |        .---------.     .----------.    .------------.     |   |
 *  | lab_img |-img_copy-->| lab_img |-----| render() |--->| wlr_buffer |-----`   |
 *  ´---------`   |        ´---------`     ´----------`    ´------------`         |
 *           \    |           /                                                   |
 *            \   ´----------/----------------------------------------------------`
 *             \            /
 *           .----------------.                       lab_img provides:
 *           |  lab_img_data  |                       - render function
 *           |   refcount=2   |                       - list of modification functions
 *           |                `-----------------.       to apply on top of lib_img_data
 *           |                                  |       when rendering
 *           | provides (depending on backend): |     - lab_img_equal() comparing the
 *           | - librsvg handle                 |       lab_img_data reference and
 *           | - cairo surface                  |       modification function pointers
 *           ´----------------------------------`       of two given lab_img instances
 *
 */

#endif /* LABWC_SCALED_IMG_BUFFER_H */
