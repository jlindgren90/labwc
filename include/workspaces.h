/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_WORKSPACES_H
#define LABWC_WORKSPACES_H

#include <wayland-server-core.h>
#include "common/refptr.h"
#include "common/str.h"

struct seat;
struct server;
struct wlr_scene_tree;

struct workspace : public ref_guarded<workspace> {
	lab_str name;
	struct wlr_scene_tree *tree;

	struct lab_cosmic_workspace *cosmic_workspace;
	struct {
		struct wl_listener activate;
		struct wl_listener deactivate;
		struct wl_listener remove;
	} on_cosmic;

	struct lab_ext_workspace *ext_workspace;
	struct {
		struct wl_listener activate;
		struct wl_listener deactivate;
		struct wl_listener assign;
		struct wl_listener remove;
	} on_ext;

	~workspace();

	bool has_views();
};

void workspaces_init(void);
void workspaces_switch_to(struct workspace *target, bool update_focus);
void workspaces_destroy(void);
void workspaces_osd_hide(void);
struct workspace *workspaces_find(struct workspace *anchor, const char *name,
	bool wrap);
void workspaces_reconfigure(void);

#endif /* LABWC_WORKSPACES_H */
