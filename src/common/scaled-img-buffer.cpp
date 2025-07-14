// SPDX-License-Identifier: GPL-2.0-only
#include "common/scaled-img-buffer.h"
#include "img/img.h"
#include "node.h"

bool
scaled_img_buffer::equal(scaled_scene_buffer &other)
{
	if (other.type != SCALED_IMG_BUFFER) {
		return false;
	}

	auto a = this;
	auto b = static_cast<scaled_img_buffer *>(&other);

	return a->img == b->img
		&& a->width == b->width
		&& a->height == b->height;
}

scaled_img_buffer::scaled_img_buffer(wlr_scene_tree *parent, const lab_img &img,
		int width, int height)
	: scaled_scene_buffer(SCALED_IMG_BUFFER, parent)
{
	assert(img.valid());
	assert(width >= 0 && height >= 0);

	this->img = img;
	this->width = width;
	this->height = height;

	scaled_scene_buffer_request_update(this, width, height);
}

struct scaled_img_buffer *
scaled_img_buffer_from_node(struct wlr_scene_node *node)
{
	struct scaled_scene_buffer *scaled_buffer =
		node_scaled_scene_buffer_from_node(node);
	assert(scaled_buffer->type == SCALED_IMG_BUFFER);
	return static_cast<scaled_img_buffer *>(scaled_buffer);
}
