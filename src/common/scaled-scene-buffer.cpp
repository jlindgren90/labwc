// SPDX-License-Identifier: GPL-2.0-only
#include "common/scaled-scene-buffer.h"
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include "common/macros.h"
#include "common/mem.h"
#include "common/reflist.h"
#include "node.h"

/*
 * This holds all the scaled_scene_buffers from all the implementers.
 * This is used to share visually duplicated buffers found via impl->equal().
 */
static reflist<scaled_scene_buffer> all_scaled_buffers;

/* Internal API */
static scaled_scene_buffer_cache::iterator
find_cache_for_scale(scaled_scene_buffer_cache &cache, double scale)
{
	return lab::find_if(cache,
		[scale](auto &entry) { return entry.scale == scale; });
}

static void
_update_buffer(struct scaled_scene_buffer *self, double scale)
{
	self->active_scale = scale;

	/* Search for cached buffer of specified scale */
	auto iter = find_cache_for_scale(self->cache, scale);
	if (iter != self->cache.end()) {
		/* LRU cache, recently used in front */
		auto cache_entry = *iter; // copy
		self->cache.erase(iter);
		self->cache.push_front(cache_entry);
		wlr_scene_buffer_set_buffer(self->scene_buffer,
			cache_entry.buffer.get());
		/*
		 * If found in our local cache,
		 * - self->width and self->height are already set
		 * - wlr_scene_buffer_set_dest_size() has already been called
		 */
		return;
	}

	refptr<lab_data_buffer> buffer;

	/* Search from other cached scaled-scene-buffers */
	for (auto &scene_buffer : all_scaled_buffers) {
		if (&scene_buffer == self) {
			continue;
		}
		if (!self->equal(scene_buffer)) {
			continue;
		}

		auto iter = find_cache_for_scale(scene_buffer.cache, scale);
		if (iter == scene_buffer.cache.end()) {
			continue;
		}

		/* Ensure self->width and self->height are set correctly */
		self->width = scene_buffer.width;
		self->height = scene_buffer.height;
		buffer = iter->buffer;
		break;
	}

	if (!buffer) {
		/* Create new buffer */
		buffer = self->create_buffer(scale);
		if (CHECK_PTR(buffer, buf)) {
			self->width = buf->logical_width;
			self->height = buf->logical_height;
		} else {
			self->width = 0;
			self->height = 0;
		}
	}

	/* Limit cache size by removing oldest entry */
	if (self->cache.size() >= LAB_SCALED_BUFFER_MAX_CACHE) {
		self->cache.pop_back();
	}

	/* Add new cache entry */
	self->cache.push_front({buffer, scale});

	/* And finally update the wlr_scene_buffer itself */
	wlr_scene_buffer_set_buffer(self->scene_buffer, buffer.get());
	wlr_scene_buffer_set_dest_size(self->scene_buffer, self->width, self->height);
}

/* Internal event handlers */
void
scaled_scene_buffer::handle_outputs_update(void *data)
{
	double max_scale = 0;
	auto event = (wlr_scene_outputs_update_event *)data;
	for (size_t i = 0; i < event->size; i++) {
		max_scale = MAX(max_scale, event->active[i]->output->scale);
	}
	if (max_scale && this->active_scale != max_scale) {
		_update_buffer(this, max_scale);
	}
}

/* Public API */
scaled_scene_buffer::scaled_scene_buffer(scaled_scene_buffer_type type,
		struct wlr_scene_tree *parent)
	: type(type)
{
	assert(parent);

	this->scene_buffer = wlr_scene_buffer_create(parent, NULL);
	die_if_null(this->scene_buffer);

	node_descriptor_create(&this->scene_buffer->node,
		LAB_NODE_DESC_SCALED_SCENE_BUFFER, this);

	/*
	 * Set active scale to zero so that we always render a new buffer when
	 * entering the first output
	 */
	this->active_scale = 0;

	all_scaled_buffers.append(this);

	/* Listen to outputs_update so we get notified about scale changes */
	CONNECT_LISTENER(this->scene_buffer, this, outputs_update);

	/* Let it destroy automatically when the scene node destroys */
	CONNECT_LISTENER(&this->scene_buffer->node, this, destroy);
}

scaled_scene_buffer::~scaled_scene_buffer()
{
	all_scaled_buffers.remove(this);
}

void
scaled_scene_buffer_request_update(struct scaled_scene_buffer *self,
		int width, int height)
{
	assert(self);
	assert(width >= 0);
	assert(height >= 0);

	self->cache.clear();

	/*
	 * Tell wlroots about the buffer size so we can receive output_enter
	 * events even when the actual backing buffer is not set yet.
	 * The buffer size set here is updated when the backing buffer is
	 * created in _update_buffer().
	 */
	wlr_scene_buffer_set_dest_size(self->scene_buffer, width, height);
	self->width = width;
	self->height = height;

	/*
	 * Skip re-rendering if the buffer is not shown yet
	 * TODO: don't re-render also when the buffer is temporarily invisible
	 */
	if (self->active_scale > 0) {
		_update_buffer(self, self->active_scale);
	}
}

void
scaled_scene_buffer_invalidate_sharing(void)
{
	all_scaled_buffers.clear();
}
