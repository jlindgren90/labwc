/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_SCENE_BUFFER_H
#define LABWC_SCALED_SCENE_BUFFER_H

#include <wayland-server-core.h>
#include "buffer.h"

#define LAB_SCALED_BUFFER_MAX_CACHE 2

struct wlr_buffer;
struct wlr_scene_tree;
struct lab_data_buffer;
struct scaled_scene_buffer;

struct scaled_scene_buffer_impl {
	/* Return a new buffer optimized for the new scale */
	refptr<lab_data_buffer> (*create_buffer)(
		struct scaled_scene_buffer *scaled_buffer, double scale);
	/* Might be NULL or used for cleaning up */
	void (*destroy)(struct scaled_scene_buffer *scaled_buffer);
	/* Returns true if the two buffers are visually the same */
	bool (*equal)(struct scaled_scene_buffer *scaled_buffer_a,
		struct scaled_scene_buffer *scaled_buffer_b);
};

struct scaled_scene_buffer {
	struct wlr_scene_buffer *scene_buffer;
	int width;   /* unscaled, read only */
	int height;  /* unscaled, read only */
	void *data;  /* opaque user data */

	/* Private */
	double active_scale;
	/* cached wlr_buffers for each scale */
	struct wl_list cache;  /* struct scaled_buffer_cache_entry.link */
	struct wl_listener destroy;
	struct wl_listener outputs_update;
	const struct scaled_scene_buffer_impl *impl;
	struct wl_list link; /* all_scaled_buffers */
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
 * Create an auto scaling buffer that creates a wlr_scene_buffer
 * and subscribes to its output_enter and output_leave signals.
 *
 * If the maximal scale changes, it either sets an already existing buffer
 * that was rendered for the current scale or - if there is none - calls
 * implementation->create_buffer(self, scale) to get a new lab_data_buffer
 * optimized for the new scale.
 *
 * Up to LAB_SCALED_BUFFER_MAX_CACHE (2) buffers are cached in an LRU fashion
 * to handle the majority of use cases where a view is moved between no more
 * than two different scales.
 *
 * scaled_scene_buffer will clean up automatically once the internal
 * wlr_scene_buffer is being destroyed. If implementation->destroy is set
 * it will also get called so a consumer of this API may clean up its own
 * allocations.
 *
 * Besides caching buffers for each scale per scaled_scene_buffer, we also
 * store all the scaled_scene_buffers from all the implementers in a list
 * in order to reuse backing buffers for visually duplicated
 * scaled_scene_buffers found via impl->equal().
 *
 * All requested lab_data_buffers via impl->create_buffer() will be stored
 * in the cache via refptr (thus prevented by refcount from being destroyed)
 * until evacuated from the cache (due to LAB_SCALED_BUFFER_MAX_CACHE
 * or the internal wlr_scene_buffer being destroyed).
 */
struct scaled_scene_buffer *scaled_scene_buffer_create(
	struct wlr_scene_tree *parent,
	const struct scaled_scene_buffer_impl *impl);

/**
 * scaled_scene_buffer_request_update - mark the buffer that needs to be
 * updated
 * @width: the width of the buffer to be rendered, in scene coordinates
 * @height: the height of the buffer to be rendered, in scene coordinates
 *
 * This function should be called when the states bound to the buffer are
 * updated and ready for rendering.
 */
void scaled_scene_buffer_request_update(struct scaled_scene_buffer *self,
	int width, int height);

/**
 * scaled_scene_buffer_invalidate_sharing - clear the list of entire cached
 * scaled_scene_buffers used to share visually dupliated buffers. This should
 * be called on Reconfigure to force updates of newly created
 * scaled_scene_buffers rather than reusing ones created before Reconfigure.
 */
void scaled_scene_buffer_invalidate_sharing(void);

/* Private */
struct scaled_scene_buffer_cache_entry {
	struct wl_list link;   /* struct scaled_scene_buffer.cache */
	refptr<lab_data_buffer> buffer;
	double scale;
};

#endif /* LABWC_SCALED_SCENE_BUFFER_H */
