// SPDX-License-Identifier: GPL-2.0-only

#include <assert.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include "common/scene-helpers.h"

struct wlr_scene_rect *
lab_wlr_scene_get_rect(struct wlr_scene_node *node)
{
	assert(node->type == WLR_SCENE_NODE_RECT);
	return (struct wlr_scene_rect *)node;
}

struct wlr_scene_tree *
lab_scene_tree_from_node(struct wlr_scene_node *node)
{
	assert(node->type == WLR_SCENE_NODE_TREE);
	return (struct wlr_scene_tree *)node;
}

struct wlr_surface *
lab_wlr_surface_from_node(struct wlr_scene_node *node)
{
	struct wlr_scene_buffer *buffer;
	struct wlr_scene_surface *scene_surface;

	if (node && node->type == WLR_SCENE_NODE_BUFFER) {
		buffer = wlr_scene_buffer_from_node(node);
		scene_surface = wlr_scene_surface_try_from_buffer(buffer);
		if (scene_surface) {
			return scene_surface->surface;
		}
	}
	return NULL;
}

struct wlr_scene_node *
lab_wlr_scene_get_prev_node(struct wlr_scene_node *node)
{
	assert(node);
	struct wlr_scene_node *prev;
	prev = wl_container_of(node->link.prev, node, link);
	if (&prev->link == &node->parent->children) {
		return NULL;
	}
	return prev;
}

bool
lab_wlr_scene_output_commit(struct wlr_scene_output *scene_output)
{
	assert(scene_output);
	struct wlr_output *wlr_output = scene_output->output;
	struct wlr_output_state *state = &wlr_output->pending;

	/*
	 * This is a copy of wlr_scene_output_commit()
	 * as it doesn't use the pending state at all.
	 */
	if (!wlr_output->needs_frame && !pixman_region32_not_empty(
			&scene_output->damage_ring.current)) {
		return false;
	}
	if (!wlr_scene_output_build_state(scene_output, state, NULL)) {
		wlr_log(WLR_ERROR, "Failed to build output state for %s",
			wlr_output->name);
		return false;
	}
	if (!wlr_output_commit(wlr_output)) {
		wlr_log(WLR_ERROR, "Failed to commit output %s",
			wlr_output->name);
		return false;
	}
	/*
	 * FIXME: Remove the following line as soon as
	 * https://gitlab.freedesktop.org/wlroots/wlroots/-/merge_requests/4253
	 * is merged. At that point wlr_scene handles damage tracking internally
	 * again.
	 */
	wlr_damage_ring_rotate(&scene_output->damage_ring);
	return true;
}
