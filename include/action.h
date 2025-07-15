/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_ACTION_H
#define LABWC_ACTION_H

#include <vector>
#include <wayland-util.h>
#include "common/str.h"

struct action_arg;
struct view;
struct view_query;
struct cursor_context;

enum action_type {
	ACTION_TYPE_INVALID = 0,
	ACTION_TYPE_NONE,
	ACTION_TYPE_CLOSE,
	ACTION_TYPE_KILL,
	ACTION_TYPE_DEBUG,
	ACTION_TYPE_EXECUTE,
	ACTION_TYPE_EXIT,
	ACTION_TYPE_MOVE_TO_EDGE,
	ACTION_TYPE_TOGGLE_SNAP_TO_EDGE,
	ACTION_TYPE_SNAP_TO_EDGE,
	ACTION_TYPE_GROW_TO_EDGE,
	ACTION_TYPE_SHRINK_TO_EDGE,
	ACTION_TYPE_NEXT_WINDOW,
	ACTION_TYPE_PREVIOUS_WINDOW,
	ACTION_TYPE_RECONFIGURE,
	ACTION_TYPE_SHOW_MENU,
	ACTION_TYPE_TOGGLE_MAXIMIZE,
	ACTION_TYPE_MAXIMIZE,
	ACTION_TYPE_UNMAXIMIZE,
	ACTION_TYPE_TOGGLE_FULLSCREEN,
	ACTION_TYPE_SET_DECORATIONS,
	ACTION_TYPE_TOGGLE_DECORATIONS,
	ACTION_TYPE_TOGGLE_ALWAYS_ON_TOP,
	ACTION_TYPE_TOGGLE_ALWAYS_ON_BOTTOM,
	ACTION_TYPE_TOGGLE_OMNIPRESENT,
	ACTION_TYPE_FOCUS,
	ACTION_TYPE_UNFOCUS,
	ACTION_TYPE_ICONIFY,
	ACTION_TYPE_MOVE,
	ACTION_TYPE_RAISE,
	ACTION_TYPE_LOWER,
	ACTION_TYPE_RESIZE,
	ACTION_TYPE_RESIZE_RELATIVE,
	ACTION_TYPE_MOVETO,
	ACTION_TYPE_RESIZETO,
	ACTION_TYPE_MOVETO_CURSOR,
	ACTION_TYPE_MOVE_RELATIVE,
	ACTION_TYPE_SEND_TO_DESKTOP,
	ACTION_TYPE_GO_TO_DESKTOP,
	ACTION_TYPE_TOGGLE_SNAP_TO_REGION,
	ACTION_TYPE_SNAP_TO_REGION,
	ACTION_TYPE_UNSNAP,
	ACTION_TYPE_TOGGLE_KEYBINDS,
	ACTION_TYPE_FOCUS_OUTPUT,
	ACTION_TYPE_MOVE_TO_OUTPUT,
	ACTION_TYPE_FIT_TO_OUTPUT,
	ACTION_TYPE_IF,
	ACTION_TYPE_FOR_EACH,
	ACTION_TYPE_VIRTUAL_OUTPUT_ADD,
	ACTION_TYPE_VIRTUAL_OUTPUT_REMOVE,
	ACTION_TYPE_AUTO_PLACE,
	ACTION_TYPE_TOGGLE_TEARING,
	ACTION_TYPE_SHADE,
	ACTION_TYPE_UNSHADE,
	ACTION_TYPE_TOGGLE_SHADE,
	ACTION_TYPE_ENABLE_SCROLL_WHEEL_EMULATION,
	ACTION_TYPE_DISABLE_SCROLL_WHEEL_EMULATION,
	ACTION_TYPE_TOGGLE_SCROLL_WHEEL_EMULATION,
	ACTION_TYPE_ENABLE_TABLET_MOUSE_EMULATION,
	ACTION_TYPE_DISABLE_TABLET_MOUSE_EMULATION,
	ACTION_TYPE_TOGGLE_TABLET_MOUSE_EMULATION,
	ACTION_TYPE_TOGGLE_MAGNIFY,
	ACTION_TYPE_ZOOM_IN,
	ACTION_TYPE_ZOOM_OUT,
	ACTION_TYPE_WARP_CURSOR,
	ACTION_TYPE_HIDE_CURSOR,
};

enum action_arg_type {
	LAB_ACTION_ARG_STR = 0,
	LAB_ACTION_ARG_BOOL,
	LAB_ACTION_ARG_INT,
	LAB_ACTION_ARG_QUERY_LIST,
	LAB_ACTION_ARG_ACTION_LIST,
};

struct action {
	action_type type;
	std::vector<action_arg> args;

	~action(); // out-of-line because ~view_query() is not visible

	static action *append_new(std::vector<action> &actions,
		const char *action_name);

	void add_str(const char *key, const char *value);
	void add_bool(const char *key, bool value);
	void add_int(const char *key, int value);

	std::vector<action> &add_actionlist(const char *key);
	std::vector<view_query> &add_querylist(const char *key);

	action_arg *get_arg(const char *key, action_arg_type type);

	lab_str get_str(const char *key, const char *default_value);
	bool get_bool(const char *key, bool default_value);
	int get_int(const char *key, int default_value);

	std::vector<action> *get_actionlist(const char *key);
	std::vector<view_query> *get_querylist(const char *key);

	void add_arg_from_xml_node(const char *nodename, const char *content);

	bool is_valid();
};

struct action_arg {
	enum action_arg_type type;
	lab_str key; // may be empty if there is just one arg

	bool bval;
	int ival;
	lab_str sval;
	std::vector<action> actions;
	std::vector<view_query> queries;
};

bool actions_contain_toggle_keybinds(std::vector<action> &actions);

/**
 * actions_run() - Run actions.
 * @activator: Target view to apply actions (e.g. Maximize, Focus etc.).
 * NULL is allowed, in which case the focused/hovered view is used.
 * @ctx: Set for action invocations via mousebindings. Used to get the
 * direction of resize or the position of the window menu button for ShowMenu
 * action.
 */
void actions_run(struct view *activator, std::vector<action> &actions,
	struct cursor_context *ctx);

#endif /* LABWC_ACTION_H */
