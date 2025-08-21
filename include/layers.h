/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_LAYERS_H
#define LABWC_LAYERS_H

#include <wayland-server-core.h>
#include <wlr/util/box.h>
#include "common/listener.h"

struct server;
struct output;
struct seat;

struct lab_layer_surface : public destroyable {
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene_layer_surface;

	bool mapped;
	/* true only inside handle_unmap() */
	bool being_unmapped;

	~lab_layer_surface();

	DECLARE_HANDLER(lab_layer_surface, map);
	DECLARE_HANDLER(lab_layer_surface, unmap);
	DECLARE_HANDLER(lab_layer_surface, commit);
	DECLARE_HANDLER(lab_layer_surface, output_destroy);
	DECLARE_HANDLER(lab_layer_surface, new_popup);
};

struct lab_layer_popup : public destroyable {
	struct wlr_xdg_popup *wlr_popup;
	struct wlr_scene_tree *scene_tree;

	/* To simplify moving popup nodes from the bottom to the top layer */
	struct wlr_box output_toplevel_sx_box;

	~lab_layer_popup();

	DECLARE_HANDLER(lab_layer_popup, commit);
	DECLARE_HANDLER(lab_layer_popup, new_popup);
	DECLARE_HANDLER(lab_layer_popup, reposition);
};

void layers_init(void);
void layers_finish(void);

void layers_arrange(struct output *output);
void layer_try_set_focus(struct wlr_layer_surface_v1 *layer_surface);

#endif /* LABWC_LAYERS_H */
