// SPDX-License-Identifier: GPL-2.0-only
/*
 * layers.c - layer-shell implementation
 *
 * Based on https://github.com/swaywm/sway
 * Copyright (C) 2019 Drew DeVault and Sway developers
 */

#include "layers.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "common/macros.h"
#include "common/mem.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "output.h"

#define LAB_LAYERSHELL_VERSION 4

static void
apply_override(struct output *output, struct wlr_box *usable_area)
{
	for (auto &override : rc.usable_area_overrides) {
		if (override.output && strcasecmp(override.output.c(),
				output->wlr_output->name)) {
			continue;
		}
		usable_area->x += override.margin.left;
		usable_area->y += override.margin.top;
		usable_area->width -=
			override.margin.left + override.margin.right;
		usable_area->height -=
			override.margin.top + override.margin.bottom;
	}
}

static void
arrange_one_layer(const struct wlr_box *full_area, struct wlr_box *usable_area,
		struct wlr_scene_tree *tree, bool exclusive)
{
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct lab_layer_surface *surface = node_layer_surface_from_node(node);
		struct wlr_scene_layer_surface_v1 *scene = surface->scene_layer_surface;
		if (!scene->layer_surface->initialized) {
			continue;
		}
		if (surface->being_unmapped) {
			continue;
		}
		if (!!scene->layer_surface->current.exclusive_zone != exclusive) {
			continue;
		}
		wlr_scene_layer_surface_v1_configure(scene, full_area, usable_area);
	}
}

/*
 * To ensure outputs/views are left in a consistent state, this
 * function should be called ONLY from output_update_usable_area()
 * or output_update_all_usable_areas().
 */
void
layers_arrange(struct output *output)
{
	assert(output);
	struct wlr_box full_area = { 0 };
	wlr_output_effective_resolution(output->wlr_output,
		&full_area.width, &full_area.height);
	struct wlr_box usable_area = full_area;

	apply_override(output, &usable_area);

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(g_server.scene, output->wlr_output);
	if (!scene_output) {
		wlr_log(WLR_DEBUG, "no wlr_scene_output");
		return;
	}

	for (int i = ARRAY_SIZE(output->layer_tree) - 1; i >= 0; i--) {
		struct wlr_scene_tree *layer = output->layer_tree[i];

		/*
		 * Process exclusive-zone clients before non-exclusive-zone
		 * clients, so that the latter give way to the former regardless
		 * of the order in which they were launched.
		 *
		 * Also start calculating the usable_area for exclusive-zone
		 * clients from the Overlay layer down to the Background layer
		 * to ensure that higher layers have a higher preference for
		 * placement.
		 *
		 * The 'exclusive' boolean also matches -1 which means that
		 * the layershell client wants to use the full screen rather
		 * than the usable area.
		 */
		arrange_one_layer(&full_area, &usable_area, layer, /* exclusive */ true);
	}

	for (size_t i = 0; i < ARRAY_SIZE(output->layer_tree); i++) {
		struct wlr_scene_tree *layer = output->layer_tree[i];
		arrange_one_layer(&full_area, &usable_area, layer, /* exclusive */ false);

		/* Set node position to account for output layout change */
		wlr_scene_node_set_position(&layer->node, scene_output->x,
			scene_output->y);
	}

	output->usable_area = usable_area;
}

void
lab_layer_surface::handle_output_destroy(void *)
{
	scene_layer_surface->layer_surface->output = NULL;
	wlr_layer_surface_v1_destroy(scene_layer_surface->layer_surface);
}

static inline bool
has_exclusive_interactivity(struct wlr_scene_layer_surface_v1 *scene)
{
	return scene->layer_surface->current.keyboard_interactive
		== ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
}

/*
 * Try to transfer focus to other layer-shell clients with exclusive focus on
 * the output nearest to the cursor. If none exist (which is likely to generally
 * be the case) just unset layer focus and try to give it to the topmost
 * toplevel if one exists.
 */
static void
try_to_focus_next_layer_or_toplevel(void)
{
	struct output *output = output_nearest_to_cursor();
	if (!output) {
		goto no_output;
	}

{ /* !goto */
	enum zwlr_layer_shell_v1_layer overlay = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
	enum zwlr_layer_shell_v1_layer top = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
	for (size_t i = overlay; i >= top; i--) {
		struct wlr_scene_tree *tree = output->layer_tree[i];
		struct wlr_scene_node *node;
		/*
		 * In wlr_scene.c they were added at end of list so we
		 * iterate in reverse to process last client first.
		 */
		wl_list_for_each_reverse(node, &tree->children, link) {
			struct lab_layer_surface *layer = node_layer_surface_from_node(node);
			struct wlr_scene_layer_surface_v1 *scene = layer->scene_layer_surface;
			struct wlr_layer_surface_v1 *layer_surface = scene->layer_surface;
			/*
			 * In case we have just come from the unmap handler and
			 * the commit has not yet been processed.
			 */
			if (!layer_surface->surface->mapped) {
				continue;
			}
			if (has_exclusive_interactivity(scene)) {
				wlr_log(WLR_DEBUG, "focus next exclusive layer client");
				seat_set_focus_layer(layer_surface);
				return;
			}
		}
	}

	/*
	 * Unfocus the current layer-surface and focus the topmost toplevel if
	 * one exists on the current workspace.
	 */
} no_output:
	if (g_seat.focused_layer) {
		seat_set_focus_layer(NULL);
	}
}

static bool
focused_layer_has_exclusive_interactivity(void)
{
	if (!g_seat.focused_layer) {
		return false;
	}
	return g_seat.focused_layer->current.keyboard_interactive
		== ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
}

/*
 * Precedence is defined as being in the same or higher (overlay is highest)
 * than the layer with current keyboard focus.
 */
static bool
has_precedence(enum zwlr_layer_shell_v1_layer layer)
{
	if (!g_seat.focused_layer) {
		return true;
	}
	if (!focused_layer_has_exclusive_interactivity()) {
		return true;
	}
	if (layer >= g_seat.focused_layer->current.layer) {
		return true;
	}
	return false;
}

void
layer_try_set_focus(struct wlr_layer_surface_v1 *layer_surface)
{
	switch (layer_surface->current.keyboard_interactive) {
	case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE:
		wlr_log(WLR_DEBUG, "interactive-exclusive '%p'", layer_surface);
		if (has_precedence(layer_surface->current.layer)) {
			seat_set_focus_layer(layer_surface);
		}
		break;
	case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND:
		wlr_log(WLR_DEBUG, "interactive-on-demand '%p'", layer_surface);
		if (!focused_layer_has_exclusive_interactivity()) {
			seat_set_focus_layer(layer_surface);
		}
		break;
	case ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE:
		wlr_log(WLR_DEBUG, "interactive-none '%p'", layer_surface);
		if (g_seat.focused_layer == layer_surface) {
			try_to_focus_next_layer_or_toplevel();
		}
		break;
	}
}

static bool
is_on_demand(struct wlr_layer_surface_v1 *layer_surface)
{
	return layer_surface->current.keyboard_interactive ==
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
}

void
lab_layer_surface::handle_commit(void *)
{
	auto layer = this;
	struct wlr_layer_surface_v1 *layer_surface =
		layer->scene_layer_surface->layer_surface;
	struct wlr_output *wlr_output =
		layer->scene_layer_surface->layer_surface->output;

	if (!wlr_output) {
		return;
	}

	uint32_t committed = layer_surface->current.committed;
	struct output *output = (struct output *)wlr_output->data;

	/* Process layer change */
	if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
		wlr_scene_node_reparent(&layer->scene_layer_surface->tree->node,
			output->layer_tree[layer_surface->current.layer]);
	}
	/* Process keyboard-interactivity change */
	if (committed & WLR_LAYER_SURFACE_V1_STATE_KEYBOARD_INTERACTIVITY) {
		/*
		 * On-demand interactivity should only be honoured through
		 * normal focus semantics (for example by surface receiving
		 * cursor-button-press).
		 */
		if (is_on_demand(layer_surface)) {
			if (g_seat.focused_layer == layer_surface) {
				/*
				 * Must be change from EXCLUSIVE to ON_DEMAND,
				 * so we should give us focus.
				 */
				try_to_focus_next_layer_or_toplevel();
			}
			goto out;
		}
		/* Handle EXCLUSIVE and NONE requests */
		layer_try_set_focus(layer_surface);
	}
out:

	if (committed || layer->mapped != layer_surface->surface->mapped) {
		layer->mapped = layer_surface->surface->mapped;
		output_update_usable_area(output);
		/*
		 * Update cursor focus here to ensure we
		 * enter a new/moved/resized layer surface.
		 */
		cursor_update_focus();
	}
}

lab_layer_surface::~lab_layer_surface()
{
	auto layer = this;

	/*
	 * If the surface of this node has the current keyboard focus, then we
	 * have to deal with `g_seat.focused_layer` to avoid UAF bugs, for
	 * example on TTY change. See issue #2863
	 */
	if (layer->layer_surface == g_seat.focused_layer) {
		seat_set_focus_layer(NULL);
	}

	/*
	 * Important:
	 *
	 * We can no longer access layer->scene_layer_surface anymore
	 * because it has already been free'd by wlroots.
	 * Set it to NULL to run into a proper crash rather than accessing
	 * random free'd memory.
	 */
	layer->scene_layer_surface = NULL;

	struct wlr_xdg_popup *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &layer->layer_surface->popups, link) {
		wlr_xdg_popup_destroy(popup);
	}

	/*
	 * TODO: Determine if this layer is being used by an exclusive client.
	 * If it is, try and find another layer owned by this client to pass
	 * focus to.
	 */
}

void
lab_layer_surface::handle_unmap(void *)
{
	auto layer = this;
	struct wlr_layer_surface_v1 *layer_surface =
		layer->scene_layer_surface->layer_surface;

	/*
	 * If we send a configure event in unmap handler, the layer-shell
	 * client sends ack_configure back and wlroots posts a
	 * "wrong configure serial" error, which terminates the client (see
	 * https://github.com/labwc/labwc/pull/1154#issuecomment-2906885183).
	 *
	 * To prevent this, we set being_unmapped here and check it in
	 * arrange_one_layer() called by output_update_usable_area().
	 */
	layer->being_unmapped = true;

	if (layer_surface->output) {
		output_update_usable_area((output *)layer_surface->output->data);
	}
	if (g_seat.focused_layer == layer_surface) {
		try_to_focus_next_layer_or_toplevel();
	}

	layer->being_unmapped = false;
}

void
lab_layer_surface::handle_map(void *)
{
	auto layer = this;
	struct wlr_output *wlr_output =
		layer->scene_layer_surface->layer_surface->output;
	if (wlr_output) {
		output_update_usable_area((output *)wlr_output->data);
	}

	/*
	 * Since moving to the wlroots scene-graph API, there is no need to
	 * call wlr_surface_send_enter() from here since that will be done
	 * automatically based on the position of the surface and outputs in
	 * the scene. See wlr_scene_surface_create() documentation.
	 */
	layer_try_set_focus(layer->scene_layer_surface->layer_surface);
}

lab_layer_popup::~lab_layer_popup()
{
	struct wlr_xdg_popup *_popup, *tmp;
	wl_list_for_each_safe(_popup, tmp, &wlr_popup->base->popups, link) {
		wlr_xdg_popup_destroy(_popup);
	}

	cursor_update_focus();
}

void
lab_layer_popup::handle_commit(void *)
{
	if (wlr_popup->base->initial_commit) {
		wlr_xdg_popup_unconstrain_from_box(wlr_popup,
			&output_toplevel_sx_box);

		/* Prevent getting called over and over again */
		on_commit.disconnect();
	}
}

void
lab_layer_popup::handle_reposition(void *)
{
	wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static struct lab_layer_popup *
create_popup(struct wlr_xdg_popup *wlr_popup, struct wlr_scene_tree *parent)
{
	auto popup = new lab_layer_popup();
	popup->wlr_popup = wlr_popup;
	popup->scene_tree =
		wlr_scene_xdg_surface_create(parent, wlr_popup->base);
	die_if_null(popup->scene_tree);

	/* In support of IME popup */
	wlr_popup->base->surface->data = popup->scene_tree;

	node_descriptor_create(&popup->scene_tree->node,
		LAB_NODE_LAYER_POPUP, /*view*/ NULL, popup);

	CONNECT_LISTENER(wlr_popup, popup, destroy);
	CONNECT_LISTENER(wlr_popup->base, popup, new_popup);
	CONNECT_LISTENER(wlr_popup->base->surface, popup, commit);
	CONNECT_LISTENER(wlr_popup, popup, reposition);

	return popup;
}

/* This popup's parent is a layer popup */
void
lab_layer_popup::handle_new_popup(void *data)
{
	struct lab_layer_popup *new_popup =
		create_popup((wlr_xdg_popup *)data, scene_tree);

	new_popup->output_toplevel_sx_box = this->output_toplevel_sx_box;
}

/*
 * We move popups from the bottom to the top layer so that they are
 * rendered above views.
 */
static void
move_popup_to_top_layer(struct lab_layer_surface *toplevel,
		struct lab_layer_popup *popup)
{
	struct wlr_output *wlr_output =
		toplevel->scene_layer_surface->layer_surface->output;
	struct output *output = (struct output *)wlr_output->data;
	struct wlr_box box = { 0 };
	wlr_output_layout_get_box(g_server.output_layout, wlr_output, &box);
	int lx = toplevel->scene_layer_surface->tree->node.x + box.x;
	int ly = toplevel->scene_layer_surface->tree->node.y + box.y;

	struct wlr_scene_node *node = &popup->scene_tree->node;
	wlr_scene_node_reparent(node, output->layer_popup_tree);
	/* FIXME: verify the whole tree should be repositioned */
	wlr_scene_node_set_position(&output->layer_popup_tree->node, lx, ly);
}

/* This popup's parent is a shell-layer surface */
void
lab_layer_surface::handle_new_popup(void *data)
{
	auto wlr_popup = (wlr_xdg_popup *)data;
	auto surface = this->scene_layer_surface;
	auto output = (struct output *)surface->layer_surface->output->data;

	int lx, ly;
	wlr_scene_node_coords(&surface->tree->node, &lx, &ly);

	struct wlr_box output_box = { 0 };
	wlr_output_layout_get_box(g_server.output_layout, output->wlr_output,
		&output_box);

	/*
	 * Output geometry expressed in the coordinate system of the toplevel
	 * parent of popup. We store this struct the lab_layer_popup struct
	 * to make it easier to unconstrain children when we move popups from
	 * the bottom to the top layer.
	 */
	struct wlr_box output_toplevel_sx_box = {
		.x = output_box.x - lx,
		.y = output_box.y - ly,
		.width = output_box.width,
		.height = output_box.height,
	};
	struct lab_layer_popup *popup = create_popup(wlr_popup, surface->tree);
	popup->output_toplevel_sx_box = output_toplevel_sx_box;

	if (surface->layer_surface->current.layer
			<= ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
		move_popup_to_top_layer(this, popup);
	}
}

static void
handle_new_layer_surface(struct wl_listener *listener, void *data)
{
	auto layer_surface = (wlr_layer_surface_v1 *)data;

	if (!layer_surface->output) {
		struct wlr_output *output =
			wlr_output_layout_output_at(g_server.output_layout,
				g_seat.cursor->x, g_seat.cursor->y);
		if (!output) {
			wlr_log(WLR_INFO,
				"No output available to assign layer surface");
			wlr_layer_surface_v1_destroy(layer_surface);
			return;
		}
		layer_surface->output = output;
	}

	auto surface = new lab_layer_surface();
	surface->layer_surface = layer_surface;

	auto output = (struct output *)layer_surface->output->data;

	wlr_fractional_scale_v1_notify_scale(layer_surface->surface,
		output->wlr_output->scale);

	struct wlr_scene_tree *selected_layer =
		output->layer_tree[layer_surface->current.layer];

	surface->scene_layer_surface = wlr_scene_layer_surface_v1_create(
		selected_layer, layer_surface);
	die_if_null(surface->scene_layer_surface);

	/* In support of IME popup */
	layer_surface->surface->data = surface->scene_layer_surface->tree;

	node_descriptor_create(&surface->scene_layer_surface->tree->node,
		LAB_NODE_LAYER_SURFACE, /*view*/ NULL, surface);

	surface->scene_layer_surface->layer_surface = layer_surface;

	CONNECT_LISTENER(layer_surface->surface, surface, commit);
	CONNECT_LISTENER(layer_surface->surface, surface, map);
	CONNECT_LISTENER(layer_surface->surface, surface, unmap);
	CONNECT_LISTENER(layer_surface, surface, new_popup);

	surface->on_output_destroy.connect(
		&layer_surface->output->events.destroy);

	CONNECT_LISTENER(&surface->scene_layer_surface->tree->node, surface,
		destroy);
}

void
layers_init(void)
{
	g_server.layer_shell = wlr_layer_shell_v1_create(g_server.wl_display,
		LAB_LAYERSHELL_VERSION);
	g_server.new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&g_server.layer_shell->events.new_surface,
		&g_server.new_layer_surface);
}

void
layers_finish(void)
{
	wl_list_remove(&g_server.new_layer_surface.link);
}
