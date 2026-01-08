/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_RCXML_H
#define LABWC_RCXML_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>
#include <libxml/tree.h>

#include "common/border.h"
#include "common/font.h"
#include "config/types.h"

#define BUTTON_MAP_MAX 16

enum adaptive_sync_mode {
	LAB_ADAPTIVE_SYNC_DISABLED,
	LAB_ADAPTIVE_SYNC_ENABLED,
	LAB_ADAPTIVE_SYNC_FULLSCREEN,
};

struct buf;

struct button_map_entry {
	uint32_t from;
	uint32_t to;
};

struct usable_area_override {
	struct border margin;
	char *output;
	struct wl_list link; /* struct rcxml.usable_area_overrides */
};

struct rcxml {
	/* from command line */
	char *config_dir;
	char *config_file;
	bool merge_config;

	/* core */
	bool xdg_shell_server_side_deco;
	bool hide_maximized_window_titlebar;
	int gap;
	enum adaptive_sync_mode adaptive_sync;
	bool auto_enable_outputs;
	bool reuse_output_mode;
	bool xwayland_persistence;
	bool primary_selection;

	/* focus */
	bool focus_follow_mouse;
	bool focus_follow_mouse_requires_movement;
	bool raise_on_focus;

	/* theme */
	char *theme_name;
	char *icon_theme_name;
	char *fallback_app_icon_name;

	struct font font_activewindow;
	struct font font_inactivewindow;
	struct font font_menuheader;
	struct font font_menuitem;
	struct font font_osd;

	/* <margin top="" bottom="" left="" right="" output="" /> */
	struct wl_list usable_area_overrides;

	/* keyboard */
	int repeat_rate;
	int repeat_delay;
	enum lab_tristate kb_numlock_enable;
	bool kb_layout_per_window;
	struct wl_list keybinds;   /* struct keybind.link */

	/* mouse */
	long doubleclick_time;     /* in ms */
	struct wl_list mousebinds; /* struct mousebind.link */

	/* libinput */
	struct wl_list libinput_categories;

	/* resistance */
	int screen_edge_strength;
	int window_edge_strength;
	int unsnap_threshold;
	int unmaximize_threshold;

	/* window snapping */
	int snap_edge_range_inner;
	int snap_edge_range_outer;
	int snap_edge_corner_range;
	bool snap_overlay_enabled;
	int snap_overlay_delay_inner;
	int snap_overlay_delay_outer;
	bool snap_top_maximize;

	int resize_corner_range;
	int resize_minimum_area;

	/* Menu */
	unsigned int menu_ignore_button_release_period;
	bool menu_show_icons;
};

extern struct rcxml rc;

void rcxml_read(const char *filename);
void rcxml_finish(void);

/*
 * Parse the child <action> nodes and append them to the list.
 * FIXME: move this function to somewhere else.
 */
void append_parsed_actions(xmlNode *node, struct wl_list *list);

#endif /* LABWC_RCXML_H */
