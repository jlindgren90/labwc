/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_RCXML_H
#define LABWC_RCXML_H

#include <vector>
#include <wayland-server-core.h>

#include "common/border.h"
#include "common/buf.h"
#include "common/font.h"
#include "common/reflist.h"
#include "common/three-state.h"
#include "config/tablet.h"
#include "config/tablet-tool.h"
#include "resize-indicator.h"
#include "ssd.h"
#include "theme.h"

struct keybind;
struct libinput_category;
struct mousebind;
struct region;
struct touch_config_entry;
struct window_rule;
struct window_switcher_field;

enum view_placement_policy {
	LAB_PLACE_INVALID = 0,
	LAB_PLACE_CENTER,
	LAB_PLACE_CURSOR,
	LAB_PLACE_AUTOMATIC,
	LAB_PLACE_CASCADE,
};

enum adaptive_sync_mode {
	LAB_ADAPTIVE_SYNC_DISABLED,
	LAB_ADAPTIVE_SYNC_ENABLED,
	LAB_ADAPTIVE_SYNC_FULLSCREEN,
};

enum tearing_mode {
	LAB_TEARING_DISABLED = 0,
	LAB_TEARING_ENABLED,
	LAB_TEARING_FULLSCREEN,
	LAB_TEARING_FULLSCREEN_FORCED,
};

enum tiling_events_mode {
	LAB_TILING_EVENTS_NEVER = 0,
	LAB_TILING_EVENTS_REGION = 1 << 0,
	LAB_TILING_EVENTS_EDGE = 1 << 1,
	LAB_TILING_EVENTS_ALWAYS =
		(LAB_TILING_EVENTS_REGION | LAB_TILING_EVENTS_EDGE),
};

/* All criteria is applied in AND logic */
enum lab_view_criteria {
	/* No filter -> all focusable views */
	LAB_VIEW_CRITERIA_NONE = 0,

	/*
	 * Includes always-on-top views, e.g.
	 * what is visible on the current workspace
	 */
	LAB_VIEW_CRITERIA_CURRENT_WORKSPACE       = 1 << 0,

	/* Positive criteria */
	LAB_VIEW_CRITERIA_FULLSCREEN              = 1 << 1,
	LAB_VIEW_CRITERIA_ALWAYS_ON_TOP           = 1 << 2,
	LAB_VIEW_CRITERIA_ROOT_TOPLEVEL           = 1 << 3,

	/* Negative criteria */
	LAB_VIEW_CRITERIA_NO_ALWAYS_ON_TOP        = 1 << 6,
	LAB_VIEW_CRITERIA_NO_SKIP_WINDOW_SWITCHER = 1 << 7,
	LAB_VIEW_CRITERIA_NO_OMNIPRESENT          = 1 << 8,
};

struct usable_area_override {
	struct border margin;
	lab_str output;
};

struct rcxml {
	/* from command line */
	char *config_dir;
	char *config_file;
	bool merge_config;

	/* core */
	bool xdg_shell_server_side_deco;
	int gap;
	enum adaptive_sync_mode adaptive_sync;
	enum tearing_mode allow_tearing;
	bool auto_enable_outputs;
	bool reuse_output_mode;
	enum view_placement_policy placement_policy;
	bool xwayland_persistence;
	bool primary_selection;
	int placement_cascade_offset_x;
	int placement_cascade_offset_y;

	/* focus */
	bool focus_follow_mouse;
	bool focus_follow_mouse_requires_movement;
	bool raise_on_focus;

	/* theme */
	lab_str theme_name;
	lab_str icon_theme_name;
	lab_str fallback_app_icon_name;
	std::vector<ssd_part_type> title_buttons_left;
	std::vector<ssd_part_type> title_buttons_right; // right-to-left
	int corner_radius;
	bool show_title;
	bool title_layout_loaded;
	bool ssd_keep_border;
	bool shadows_enabled;
	bool shadows_on_tiled;
	struct font font_activewindow;
	struct font font_inactivewindow;
	struct font font_menuheader;
	struct font font_menuitem;
	struct font font_osd;

	/* <margin top="" bottom="" left="" right="" output="" /> */
	std::vector<usable_area_override> usable_area_overrides;

	/* keyboard */
	int repeat_rate;
	int repeat_delay;
	enum three_state kb_numlock_enable;
	bool kb_layout_per_window;
	std::vector<keybind> keybinds;

	/* mouse */
	long doubleclick_time;     /* in ms */
	std::vector<mousebind> mousebinds;

	/* touch tablet */
	std::vector<touch_config_entry> touch_configs;

	/* graphics tablet */
	struct tablet_config {
		bool force_mouse_emulation;
		lab_str output_name;
		struct wlr_fbox box;
		enum rotation rotation;
		uint16_t button_map_count;
		struct button_map_entry button_map[BUTTON_MAP_MAX];
	} tablet;
	struct tablet_tool_config {
		enum motion motion;
		double relative_motion_sensitivity;
	} tablet_tool;

	/* libinput */
	std::vector<libinput_category> libinput_categories;

	/* resistance */
	int screen_edge_strength;
	int window_edge_strength;
	int unsnap_threshold;
	int unmaximize_threshold;

	/* window snapping */
	int snap_edge_range;
	bool snap_overlay_enabled;
	int snap_overlay_delay_inner;
	int snap_overlay_delay_outer;
	bool snap_top_maximize;
	enum tiling_events_mode snap_tiling_events_mode;

	enum resize_indicator_mode resize_indicator;
	bool resize_draw_contents;
	int resize_corner_range;
	int resize_minimum_area;

	struct {
		int popuptime;
		int min_nr_workspaces;
		lab_str prefix;
		struct wl_list workspaces;  /* struct workspace.link */
	} workspace_config;

	/* Regions */
	reflist<region> regions;

	/* Window Switcher */
	struct {
		bool show;
		bool preview;
		bool outlines;
		enum lab_view_criteria criteria;
		std::vector<window_switcher_field> fields;
	} window_switcher;

	std::vector<window_rule> window_rules;

	/* Menu */
	unsigned int menu_ignore_button_release_period;
	bool menu_show_icons;

	/* Magnifier */
	int mag_width;
	int mag_height;
	float mag_scale;
	float mag_increment;
	bool mag_filter;
};

extern struct rcxml rc;

void rcxml_parse_xml(struct buf *b);
void rcxml_read(const char *filename);
void rcxml_finish(void);

#endif /* LABWC_RCXML_H */
