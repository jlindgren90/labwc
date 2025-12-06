/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_BUFFER_H
#define LABWC_SCALED_BUFFER_H

#include <list>
#include "buffer.h"
#include "common/listener.h"

#define LAB_SCALED_BUFFER_MAX_CACHE 2

struct wlr_scene_buffer;
struct wlr_scene_tree;

enum scaled_buffer_type {
	SCALED_FONT_BUFFER,
	SCALED_ICON_BUFFER,
	SCALED_IMG_BUFFER,
};

/* Private */
struct scaled_buffer_cache_entry {
	refptr<lab_data_buffer> buffer;
	double scale;
};

using scaled_buffer_cache = std::list<scaled_buffer_cache_entry>;

struct scaled_buffer : public destroyable, public ref_guarded<scaled_buffer> {
	const scaled_buffer_type type;
	wlr_scene_buffer *scene_buffer = nullptr;

	int width = 0;	// unscaled, read only
	int height = 0; // unscaled, read only

	double active_scale = 0;
	// cached wlr_buffers for each scale
	scaled_buffer_cache cache;

	scaled_buffer(scaled_buffer_type type, wlr_scene_tree *parent);
	virtual ~scaled_buffer();

	// Returns a new buffer optimized for the new scale
	virtual refptr<lab_data_buffer> create_buffer(double scale) = 0;
	// Returns true if the two buffers are visually the same
	virtual bool equal(scaled_buffer &other) = 0;

	DECLARE_HANDLER(scaled_buffer, outputs_update);
};

/*
 *                                  |                 |
 *                        .------------------.  .------------.
 *       scaled_buffer    | new_output_scale |  | set_buffer |
 *       architecture     ´------------------`  ´------------`
 *                                  |                ^
 *    .-----------------------------|----------------|-----------.
 *    |                             v                |           |
 *    |  .---------------.    .-------------------------.        |
 *    |  | scaled_buffer |----| wlr_buffer LRU cache(2) |<---,   |
 *    |  ´---------------`    ´-------------------------`    |   |
 *    |           |                       |                  |   |
 *    |        .------.       .--------------------------.   |   |
 *    |        | impl |       | wlr_buffer LRU cache of  |   |   |
 *    |        ´------`       |   other scaled_buffers   |   |   |
 *    |                       |   with impl->equal()     |   |   |
 *    |                       ´--------------------------`   |   |
 *    |                          /              |            |   |
 *    |                   not found           found          |   |
 *    |     .-----------------------.     .-----------.      |   |
 *    |     | impl->create_buffer() |--->| wlr_buffer |------`   |
 *    |     ´-----------------------`    ´------------`          |
 *    |                                                          |
 *    ´----------------------------------------------------------`
 */

/**
 * scaled_buffer is an auto scaling buffer providing a wlr_scene_buffer
 * and subscribing to its output_enter and output_leave signals.
 *
 * If the maximal scale changes, it either sets an already existing buffer
 * that was rendered for the current scale or - if there is none - calls
 * the virtual function create_buffer(scale) to get a new lab_data_buffer
 * optimized for the new scale.
 *
 * Up to LAB_SCALED_BUFFER_MAX_CACHE (2) buffers are cached in an LRU fashion
 * to handle the majority of use cases where a view is moved between no more
 * than two different scales.
 *
 * scaled_buffer will clean up automatically once the internal
 * wlr_scene_buffer is being destroyed.
 *
 * Besides caching buffers for each scale per scaled_buffer, we also
 * store all the scaled_buffers from all the implementers in a list
 * in order to reuse backing buffers for visually duplicated
 * scaled_buffers found via equal().
 *
 * All requested lab_data_buffers via create_buffer() will be stored
 * in the cache via refptr (thus prevented by refcount from being destroyed)
 * until evacuated from the cache (due to LAB_SCALED_BUFFER_MAX_CACHE
 * or the internal wlr_scene_buffer being destroyed).
 */

/**
 * scaled_buffer_request_update - mark the buffer that needs to be
 * updated
 * @width: the width of the buffer to be rendered, in scene coordinates
 * @height: the height of the buffer to be rendered, in scene coordinates
 *
 * This function should be called when the states bound to the buffer are
 * updated and ready for rendering.
 */
void scaled_buffer_request_update(struct scaled_buffer *self,
	int width, int height);

/**
 * scaled_buffer_invalidate_sharing - clear the list of entire cached
 * scaled_buffers used to share visually dupliated buffers. This should
 * be called on Reconfigure to force updates of newly created
 * scaled_buffers rather than reusing ones created before Reconfigure.
 */
void scaled_buffer_invalidate_sharing(void);

#endif /* LABWC_SCALED_BUFFER_H */
