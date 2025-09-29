// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "action.h"
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_scene.h>
#include "action-prompt-codes.h"
#include "common/buf.h"
#include "common/macros.h"
#include "common/parse-bool.h"
#include "common/reflist.h"
#include "common/spawn.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "cycle.h"
#include "debug.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "magnifier.h"
#include "menu/menu.h"
#include "output.h"
#include "output-virtual.h"
#include "regions.h"
#include "ssd.h"
#include "theme.h"
#include "translate.h"
#include "view.h"
#include "workspaces.h"

const char *action_names[] = {
	"INVALID",
	"None",
	"Close",
	"Kill",
	"Debug",
	"Execute",
	"Exit",
	"MoveToEdge",
	"ToggleSnapToEdge",
	"SnapToEdge",
	"GrowToEdge",
	"ShrinkToEdge",
	"NextWindow",
	"PreviousWindow",
	"Reconfigure",
	"ShowMenu",
	"ToggleMaximize",
	"Maximize",
	"UnMaximize",
	"ToggleFullscreen",
	"SetDecorations",
	"ToggleDecorations",
	"ToggleAlwaysOnTop",
	"ToggleAlwaysOnBottom",
	"ToggleOmnipresent",
	"Focus",
	"Unfocus",
	"Iconify",
	"Move",
	"Raise",
	"Lower",
	"Resize",
	"ResizeRelative",
	"MoveTo",
	"ResizeTo",
	"MoveToCursor",
	"MoveRelative",
	"SendToDesktop",
	"GoToDesktop",
	"ToggleSnapToRegion",
	"SnapToRegion",
	"UnSnap",
	"ToggleKeybinds",
	"FocusOutput",
	"MoveToOutput",
	"FitToOutput",
	"If",
	"ForEach",
	"VirtualOutputAdd",
	"VirtualOutputRemove",
	"AutoPlace",
	"ToggleTearing",
	"Shade",
	"Unshade",
	"ToggleShade",
	"EnableScrollWheelEmulation",
	"DisableScrollWheelEmulation",
	"ToggleScrollWheelEmulation",
	"EnableTabletMouseEmulation",
	"DisableTabletMouseEmulation",
	"ToggleTabletMouseEmulation",
	"ToggleMagnify",
	"ZoomIn",
	"ZoomOut",
	"WarpCursor",
	"HideCursor",
	NULL
};

action::~action() {}

void
action::add_str(const char *key, const char *value)
{
	assert(key);
	args.push_back({
		.type = LAB_ACTION_ARG_STR,
		.key = lab_str(key),
		.sval = lab_str(value)
	});
}

void
action::add_bool(const char *key, bool value)
{
	assert(key);
	args.push_back({
		.type = LAB_ACTION_ARG_BOOL,
		.key = lab_str(key),
		.bval = value
	});
}

void
action::add_int(const char *key, int value)
{
	assert(key);
	args.push_back({
		.type = LAB_ACTION_ARG_INT,
		.key = lab_str(key),
		.ival = value
	});
}

std::vector<view_query> &
action::add_querylist(const char *key)
{
	assert(key);
	args.push_back({
		.type = LAB_ACTION_ARG_QUERY_LIST,
		.key = lab_str(key)
	});
	return args.back().queries;
}

std::vector<action> &
action::add_actionlist(const char *key)
{
	assert(key);
	args.push_back({
		.type = LAB_ACTION_ARG_ACTION_LIST,
		.key = lab_str(key)
	});
	return args.back().actions;
}

action_arg *
action::get_arg(const char *key, action_arg_type type)
{
	assert(key);
	for (auto &arg : args) {
		if (!strcasecmp(key, arg.key.c()) && arg.type == type) {
			return &arg;
		}
	}
	return NULL;
}

lab_str
action::get_str(const char *key, const char *default_value)
{
	auto arg = get_arg(key, LAB_ACTION_ARG_STR);
	return arg ? arg->sval : lab_str(default_value);
}

bool
action::get_bool(const char *key, bool default_value)
{
	auto arg = get_arg(key, LAB_ACTION_ARG_BOOL);
	return arg ? arg->bval : default_value;
}

int
action::get_int(const char *key, int default_value)
{
	auto arg = get_arg(key, LAB_ACTION_ARG_INT);
	return arg ? arg->ival : default_value;
}

std::vector<view_query> *
action::get_querylist(const char *key)
{
	auto arg = get_arg(key, LAB_ACTION_ARG_QUERY_LIST);
	return arg ? &arg->queries : NULL;
}

std::vector<action> *
action::get_actionlist(const char *key)
{
	auto arg = get_arg(key, LAB_ACTION_ARG_ACTION_LIST);
	return arg ? &arg->actions : NULL;
}

void
action::add_arg_from_xml_node(const char *nodename, const char *content)
{
	lab_str buf(nodename);
	string_truncate_at_pattern(buf.data(), ".action");
	const char *argument = buf.c();

	switch (type) {
	case ACTION_TYPE_EXECUTE:
		/*
		 * <action name="Execute"> with an <execute> child is
		 * deprecated, but we support it anyway for backward
		 * compatibility with old openbox-menu generators
		 */
		if (!strcmp(argument, "command") || !strcmp(argument, "execute")) {
			add_str("command", content);
			return;
		}
		break;
	case ACTION_TYPE_MOVE_TO_EDGE:
	case ACTION_TYPE_TOGGLE_SNAP_TO_EDGE:
	case ACTION_TYPE_SNAP_TO_EDGE:
	case ACTION_TYPE_GROW_TO_EDGE:
	case ACTION_TYPE_SHRINK_TO_EDGE:
		if (!strcmp(argument, "direction")) {
			bool tiled = (type == ACTION_TYPE_TOGGLE_SNAP_TO_EDGE
				|| type == ACTION_TYPE_SNAP_TO_EDGE);
			enum lab_edge edge = lab_edge_parse(content, tiled, /*any*/ false);
			if (edge == LAB_EDGE_NONE) {
				wlr_log(WLR_ERROR,
					"Invalid argument for action %s: '%s' (%s)",
					action_names[type], argument, content);
			} else {
				add_int(argument, edge);
			}
			return;
		}
		if (type == ACTION_TYPE_MOVE_TO_EDGE
				&& !strcasecmp(argument, "snapWindows")) {
			add_bool(argument, parse_bool(content, true));
			return;
		}
		if ((type == ACTION_TYPE_SNAP_TO_EDGE
					|| type == ACTION_TYPE_TOGGLE_SNAP_TO_EDGE)
				&& !strcasecmp(argument, "combine")) {
			add_bool(argument, parse_bool(content, false));
			return;
		}
		break;
	case ACTION_TYPE_SHOW_MENU:
		if (!strcmp(argument, "menu")) {
			add_str(argument, content);
			return;
		}
		if (!strcasecmp(argument, "atCursor")) {
			add_bool(argument, parse_bool(content, true));
			return;
		}
		if (!strcasecmp(argument, "x.position")) {
			add_str(argument, content);
			return;
		}
		if (!strcasecmp(argument, "y.position")) {
			add_str(argument, content);
			return;
		}
		break;
	case ACTION_TYPE_TOGGLE_MAXIMIZE:
	case ACTION_TYPE_MAXIMIZE:
	case ACTION_TYPE_UNMAXIMIZE:
		if (!strcmp(argument, "direction")) {
			enum view_axis axis = view_axis_parse(content);
			if (axis == VIEW_AXIS_NONE || axis == VIEW_AXIS_INVALID) {
				wlr_log(WLR_ERROR,
					"Invalid argument for action %s: '%s' (%s)",
					action_names[type], argument, content);
			} else {
				add_int(argument, axis);
			}
			return;
		}
		break;
	case ACTION_TYPE_SET_DECORATIONS:
		if (!strcmp(argument, "decorations")) {
			enum lab_ssd_mode mode = ssd_mode_parse(content);
			if (mode != LAB_SSD_MODE_INVALID) {
				add_int(argument, mode);
			} else {
				wlr_log(WLR_ERROR,
					"Invalid argument for action %s: '%s' (%s)",
					action_names[type], argument, content);
			}
			return;
		}
		if (!strcasecmp(argument, "forceSSD")) {
			add_bool(argument, parse_bool(content, false));
			return;
		}
		break;
	case ACTION_TYPE_RESIZE:
		if (!strcmp(argument, "direction")) {
			enum lab_edge edge = lab_edge_parse(content,
				/*tiled*/ true, /*any*/ false);
			if (edge == LAB_EDGE_NONE || edge == LAB_EDGE_CENTER) {
				wlr_log(WLR_ERROR,
					"Invalid argument for action %s: '%s' (%s)",
					action_names[type], argument, content);
			} else {
				add_int(argument, edge);
			}
			return;
		}
		break;
	case ACTION_TYPE_RESIZE_RELATIVE:
		if (!strcmp(argument, "left") || !strcmp(argument, "right") ||
				!strcmp(argument, "top") || !strcmp(argument, "bottom")) {
			add_int(argument, atoi(content));
			return;
		}
		break;
	case ACTION_TYPE_MOVETO:
	case ACTION_TYPE_MOVE_RELATIVE:
		if (!strcmp(argument, "x") || !strcmp(argument, "y")) {
			add_int(argument, atoi(content));
			return;
		}
		break;
	case ACTION_TYPE_RESIZETO:
		if (!strcmp(argument, "width") || !strcmp(argument, "height")) {
			add_int(argument, atoi(content));
			return;
		}
		break;
	case ACTION_TYPE_SEND_TO_DESKTOP:
		if (!strcmp(argument, "follow")) {
			add_bool(argument, parse_bool(content, true));
			return;
		}
		[[fallthrough]];
	case ACTION_TYPE_GO_TO_DESKTOP:
		if (!strcmp(argument, "to")) {
			add_str(argument, content);
			return;
		}
		if (!strcmp(argument, "wrap")) {
			add_bool(argument, parse_bool(content, true));
			return;
		}
		if (!strcmp(argument, "toggle")) {
			add_bool(argument, parse_bool(content, false));
			return;
		}
		break;
	case ACTION_TYPE_TOGGLE_SNAP_TO_REGION:
	case ACTION_TYPE_SNAP_TO_REGION:
		if (!strcmp(argument, "region")) {
			add_str(argument, content);
			return;
		}
		break;
	case ACTION_TYPE_FOCUS_OUTPUT:
	case ACTION_TYPE_MOVE_TO_OUTPUT:
		if (!strcmp(argument, "output")) {
			add_str(argument, content);
			return;
		}
		if (!strcmp(argument, "direction")) {
			enum lab_edge edge = lab_edge_parse(content,
				/*tiled*/ false, /*any*/ false);
			if (edge == LAB_EDGE_NONE) {
				wlr_log(WLR_ERROR,
					"Invalid argument for action %s: '%s' (%s)",
					action_names[type], argument, content);
			} else {
				add_int(argument, edge);
			}
			return;
		}
		if (!strcmp(argument, "wrap")) {
			add_bool(argument, parse_bool(content, false));
			return;
		}
		break;
	case ACTION_TYPE_VIRTUAL_OUTPUT_ADD:
	case ACTION_TYPE_VIRTUAL_OUTPUT_REMOVE:
		if (!strcmp(argument, "output_name")) {
			add_str(argument, content);
			return;
		}
		break;
	case ACTION_TYPE_AUTO_PLACE:
		if (!strcmp(argument, "policy")) {
			enum lab_placement_policy policy =
				view_placement_parse(content);
			if (policy == LAB_PLACE_INVALID) {
				wlr_log(WLR_ERROR,
					"Invalid argument for action %s: '%s' (%s)",
					action_names[type], argument, content);
			} else {
				add_int(argument, policy);
			}
			return;
		}
		break;
	case ACTION_TYPE_WARP_CURSOR:
		if (!strcmp(argument, "to") || !strcmp(argument, "x") || !strcmp(argument, "y")) {
			add_str(argument, content);
			return;
		}
		break;
	case ACTION_TYPE_IF:
		if (!strcmp(argument, "message.prompt")) {
			add_str("message.prompt", content);
		}
		return;
	default:
		break;
	}

	wlr_log(WLR_ERROR, "Invalid argument for action %s: '%s'",
		action_names[type], argument);
}

static enum action_type
action_type_from_str(const char *action_name)
{
	for (size_t i = 1; action_names[i]; i++) {
		if (!strcasecmp(action_name, action_names[i])) {
			return (action_type)i;
		}
	}
	wlr_log(WLR_ERROR, "Invalid action: %s", action_name);
	return ACTION_TYPE_INVALID;
}

action *
action::append_new(std::vector<action> &actions, const char *action_name)
{
	if (!action_name) {
		wlr_log(WLR_ERROR, "action name not specified");
		return nullptr;
	}

	enum action_type action_type = action_type_from_str(action_name);
	if (action_type == ACTION_TYPE_NONE) {
		return nullptr;
	}

	actions.push_back({.type = action_type});
	return &actions.back();
}

bool
actions_contain_toggle_keybinds(std::vector<action> &actions)
{
	for (auto &action : actions) {
		if (action.type == ACTION_TYPE_TOGGLE_KEYBINDS) {
			return true;
		}
	}
	return false;
}

static bool
action_list_is_valid(std::vector<action> &actions)
{
	for (auto &action : actions) {
		if (!action.is_valid()) {
			return false;
		}
	}
	return true;
}

static bool
action_branches_are_valid(action &action)
{
	static const char * const branches[] = { "then", "else", "none" };
	for (size_t i = 0; i < ARRAY_SIZE(branches); i++) {
		auto children = action.get_actionlist(branches[i]);
		if (children && !action_list_is_valid(*children)) {
			wlr_log(WLR_ERROR, "Invalid action in %s '%s' branch",
				action_names[action.type], branches[i]);
			return false;
		}
	}
	return true;
}

/* Checks for *required* arguments */
bool
action::is_valid()
{
	const char *arg_name = NULL;
	enum action_arg_type arg_type = LAB_ACTION_ARG_STR;

	switch (type) {
	case ACTION_TYPE_EXECUTE:
		arg_name = "command";
		break;
	case ACTION_TYPE_MOVE_TO_EDGE:
	case ACTION_TYPE_TOGGLE_SNAP_TO_EDGE:
	case ACTION_TYPE_SNAP_TO_EDGE:
	case ACTION_TYPE_GROW_TO_EDGE:
	case ACTION_TYPE_SHRINK_TO_EDGE:
		arg_name = "direction";
		arg_type = LAB_ACTION_ARG_INT;
		break;
	case ACTION_TYPE_SHOW_MENU:
		arg_name = "menu";
		break;
	case ACTION_TYPE_GO_TO_DESKTOP:
	case ACTION_TYPE_SEND_TO_DESKTOP:
		arg_name = "to";
		break;
	case ACTION_TYPE_TOGGLE_SNAP_TO_REGION:
	case ACTION_TYPE_SNAP_TO_REGION:
		arg_name = "region";
		break;
	case ACTION_TYPE_IF:
	case ACTION_TYPE_FOR_EACH:
		return action_branches_are_valid(*this);
	default:
		/* No arguments required */
		return true;
	}

	if (get_arg(arg_name, arg_type)) {
		return true;
	}

	wlr_log(WLR_ERROR, "Missing required argument for %s: %s",
		action_names[type], arg_name);
	return false;
}

static void
show_menu(struct view *view, struct cursor_context *ctx, const char *menu_name,
		bool at_cursor, const char *pos_x, const char *pos_y)
{
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH
			&& g_server.input_mode != LAB_INPUT_STATE_MENU) {
		/* Prevent opening a menu while resizing / moving a view */
		return;
	}

	struct menu *menu = menu_get_by_id(menu_name);
	if (!menu) {
		return;
	}

	int x = g_seat.cursor->x;
	int y = g_seat.cursor->y;

	/* The client menu needs an active client */
	bool is_client_menu = !strcasecmp(menu_name, "client-menu");
	if (is_client_menu && !view) {
		return;
	}
	/* Place menu in the view corner if desired (and menu is not root-menu) */
	if (!at_cursor && view) {
		struct wlr_box extent = ssd_max_extents(view);
		x = extent.x;
		y = view->current.y;
		/* Push the client menu underneath the button */
		if (is_client_menu && node_type_contains(
				LAB_NODE_BUTTON, ctx->type)) {
			assert(ctx->node);
			int lx, ly;
			wlr_scene_node_coords(ctx->node, &lx, &ly);
			/* MAX() prevents negative x when the window is maximized */
			x = MAX(x, lx - g_theme.menu_border_width);
		}
	}

	/*
	 * determine placement by looking at x and y
	 * x/y can be number, "center" or a %percent of screen dimensions
	 */
	if (pos_x && pos_y && pos_x[0] && pos_y[0]) {
		struct output *output =
			output_nearest_to(g_seat.cursor->x, g_seat.cursor->y);
		struct wlr_box usable = output_usable_area_in_layout_coords(output);

		if (!strcasecmp(pos_x, "center")) {
			x = (usable.width - menu->size.width) / 2;
		} else if (strchr(pos_x, '%')) {
			x = (usable.width * atoi(pos_x)) / 100;
		} else {
			if (pos_x[0] == '-') {
				int neg_x = strtol(pos_x, NULL, 10);
				x = usable.width + neg_x;
			} else {
				x = atoi(pos_x);
			}
		}

		if (!strcasecmp(pos_y, "center")) {
			y = (usable.height / 2) - (menu->size.height / 2);
		} else if (strchr(pos_y, '%')) {
			y = (usable.height * atoi(pos_y)) / 100;
		} else {
			if (pos_y[0] == '-') {
				int neg_y = strtol(pos_y, NULL, 10);
				y = usable.height + neg_y;
			} else {
				y = atoi(pos_y);
			}
		}
		/* keep menu from being off screen */
		x = MAX(x, 0);
		x = MIN(x, usable.width - 1);
		y = MAX(y, 0);
		y = MIN(y, usable.height - 1);
		/* adjust for which monitor to appear on */
		x += usable.x;
		y += usable.y;
	}

	/* Replaced by next show_menu() or cleaned on view_destroy() */
	menu->triggered_by_view = view;
	menu_open_root(menu, x, y);
}

static struct view *
view_for_action(struct view *activator, action &action,
		struct cursor_context *ctx)
{
	/* View is explicitly specified for mousebinds */
	if (activator) {
		return activator;
	}

	/* Select view based on action type for keybinds */
	switch (action.type) {
	case ACTION_TYPE_FOCUS:
	case ACTION_TYPE_MOVE:
	case ACTION_TYPE_RESIZE: {
		*ctx = get_cursor_context();
		return ctx->view;
	}
	default:
		return g_server.active_view;
	}
}

struct action_prompt : public ref_guarded<action_prompt> {
	/* Set when created */
	struct action *action;
	refptr<::view> view;

	/* Set when executed */
	pid_t pid;

	/* view destroyed */
	DECLARE_HANDLER(action_prompt, destroy);
};

static ownlist<action_prompt> prompts;

void
action_prompt::handle_destroy(void *)
{
	// view was destroyed
	view.reset();
	on_destroy.disconnect();
}

static lab_str
print_prompt_command(const char *format, action &action)
{
	assert(format);
	lab_str buf;

	for (const char *p = format; *p; p++) {
		/*
		 * If we're not on a conversion specifier (like %m) then just
		 * keep adding it to the buffer
		 */
		if (*p != '%') {
			buf += *p;
			continue;
		}

		/* Process the %* conversion specifier */
		++p;

		switch (*p) {
		case 'm':
			buf += action.get_str("message.prompt",
				"Choose wisely");
			break;
		case 'n':
			buf += _("No");
			break;
		case 'y':
			buf += _("Yes");
			break;
		case 'b':
			buf += hex_color_to_str(g_theme.osd_bg_color);
			break;
		case 't':
			buf += hex_color_to_str(g_theme.osd_label_text_color);
			break;
		default:
			wlr_log(WLR_ERROR,
				"invalid prompt command conversion specifier '%c'", *p);
			break;
		}
	}

	return buf;
}

static void
action_prompt_create(struct view *view, action &action)
{
	lab_str command = print_prompt_command(rc.prompt_command.c(), action);

	wlr_log(WLR_INFO, "prompt command: '%s'", command.c());

	int pipe_fd;
	pid_t prompt_pid = spawn_piped(command.c(), &pipe_fd);
	if (prompt_pid < 0) {
		wlr_log(WLR_ERROR, "Failed to create action prompt");
		return;
	}
	/* FIXME: closing stdout might confuse clients */
	close(pipe_fd);

	auto prompt = new action_prompt();
	prompt->action = &action;
	prompt->view.reset(view);
	prompt->pid = prompt_pid;
	if (view) {
		CONNECT_LISTENER(view, prompt, destroy);
	}

	prompts.append(prompt);
}

void
action_prompts_destroy(void)
{
	prompts.clear();
}

bool
action_check_prompt_result(pid_t pid, int exit_code)
{
	for (auto prompt = prompts.begin(); prompt; ++prompt) {
		if (prompt->pid != pid) {
			continue;
		}

		wlr_log(WLR_INFO, "Found pending prompt for exit code %d", exit_code);
		std::vector<action> *actions = NULL;
		if (exit_code == LAB_EXIT_SUCCESS) {
			wlr_log(WLR_INFO, "Selected the 'then' branch");
			actions = prompt->action->get_actionlist("then");
		} else if (exit_code == LAB_EXIT_CANCELLED) {
			/* no-op */
		} else {
			wlr_log(WLR_INFO, "Selected the 'else' branch");
			actions = prompt->action->get_actionlist("else");
		}
		if (actions) {
			wlr_log(WLR_INFO, "Running actions");
			actions_run(prompt->view.get(), *actions,
				/*cursor_ctx*/ NULL);
		} else {
			wlr_log(WLR_INFO, "No actions for selected branch");
		}
		prompt.remove();
		return true;
	}
	return false;
}

static bool
match_queries(struct view *view, action &action)
{
	assert(view);

	auto queries = action.get_querylist("query");
	if (!queries) {
		return true;
	}

	/* All queries are OR'ed */
	for (auto &query : *queries) {
		if (view_matches_query(view, &query)) {
			return true;
		}
	}
	return false;
}

static struct output *
get_target_output(struct output *output, action &action)
{
	lab_str output_name = action.get_str("output", NULL);
	struct output *target = NULL;

	if (output_name) {
		target = output_from_name(output_name.c());
	} else {
		auto edge = (lab_edge)action.get_int("direction", LAB_EDGE_NONE);
		bool wrap = action.get_bool("wrap", false);
		target = output_get_adjacent(output, edge, wrap);
	}

	if (!target) {
		wlr_log(WLR_DEBUG, "Invalid output");
	}

	return target;
}

static void
warp_cursor(struct view *view, const char *to, const char *x, const char *y)
{
	struct output *output = output_nearest_to_cursor();
	struct wlr_box target_area = {0};
	int goto_x;
	int goto_y;

	if (!strcasecmp(to, "output") && output) {
		target_area = output_usable_area_in_layout_coords(output);
	} else if (!strcasecmp(to, "window") && view) {
		target_area = view->current;
	} else {
		wlr_log(WLR_ERROR, "Invalid argument for action WarpCursor: 'to' (%s)", to);
	}

	if (!strcasecmp(x, "center")) {
		goto_x = target_area.x + target_area.width / 2;
	} else {
		int offset_x = atoi(x);
		goto_x = offset_x >= 0 ?
			target_area.x + offset_x :
			target_area.x + target_area.width + offset_x;
	}

	if (!strcasecmp(y, "center")) {
		goto_y = target_area.y + target_area.height / 2;
	} else {
		int offset_y = atoi(y);
		goto_y = offset_y >= 0 ?
			target_area.y + offset_y :
			target_area.y + target_area.height + offset_y;
	}

	wlr_cursor_warp(g_seat.cursor, NULL, goto_x, goto_y);
	cursor_update_focus();
}

static void
run_action(struct view *view, action &action, struct cursor_context *ctx)
{
	switch (action.type) {
	case ACTION_TYPE_CLOSE:
		if (view) {
			view_close(view);
		}
		break;
	case ACTION_TYPE_KILL:
		if (view) {
			/* Send SIGTERM to the process associated with the surface */
			pid_t pid = view->get_pid();
			if (pid == getpid()) {
				wlr_log(WLR_ERROR, "Preventing sending SIGTERM to labwc");
			} else if (pid > 0) {
				kill(pid, SIGTERM);
			}
		}
		break;
	case ACTION_TYPE_DEBUG:
		debug_dump_scene();
		break;
	case ACTION_TYPE_EXECUTE: {
		lab_str cmd = action.get_str("command", NULL);
		cmd = buf_expand_tilde(cmd.c());
		spawn_async_no_shell(cmd.c());
		break;
	}
	case ACTION_TYPE_EXIT:
		wl_display_terminate(g_server.wl_display);
		break;
	case ACTION_TYPE_MOVE_TO_EDGE:
		if (view) {
			/* Config parsing makes sure that direction is a valid direction */
			auto edge = (lab_edge)action.get_int("direction", 0);
			bool snap_to_windows = action.get_bool("snapWindows", true);
			view_move_to_edge(view, edge, snap_to_windows);
		}
		break;
	case ACTION_TYPE_TOGGLE_SNAP_TO_EDGE:
	case ACTION_TYPE_SNAP_TO_EDGE:
		if (view) {
			/* Config parsing makes sure that direction is a valid direction */
			auto edge = (lab_edge)action.get_int("direction", 0);
			if (action.type == ACTION_TYPE_TOGGLE_SNAP_TO_EDGE
					&& view->maximized == VIEW_AXIS_NONE
					&& !view->fullscreen
					&& view_is_tiled(view)
					&& view->tiled == edge) {
				view_set_untiled(view);
				view_apply_natural_geometry(view);
				break;
			}
			bool combine = action.get_bool("combine", false);
			view_snap_to_edge(view, edge, /*across_outputs*/ true,
				combine);
		}
		break;
	case ACTION_TYPE_GROW_TO_EDGE:
		if (view) {
			/* Config parsing makes sure that direction is a valid direction */
			auto edge = (lab_edge)action.get_int("direction", 0);
			view_grow_to_edge(view, edge);
		}
		break;
	case ACTION_TYPE_SHRINK_TO_EDGE:
		if (view) {
			/* Config parsing makes sure that direction is a valid direction */
			auto edge = (lab_edge)action.get_int("direction", 0);
			view_shrink_to_edge(view, edge);
		}
		break;
	case ACTION_TYPE_NEXT_WINDOW:
		if (g_server.input_mode == LAB_INPUT_STATE_CYCLE) {
			cycle_step(LAB_CYCLE_DIR_FORWARD);
		} else {
			cycle_begin(LAB_CYCLE_DIR_FORWARD);
		}
		break;
	case ACTION_TYPE_PREVIOUS_WINDOW:
		if (g_server.input_mode == LAB_INPUT_STATE_CYCLE) {
			cycle_step(LAB_CYCLE_DIR_BACKWARD);
		} else {
			cycle_begin(LAB_CYCLE_DIR_BACKWARD);
		}
		break;
	case ACTION_TYPE_RECONFIGURE:
		kill(getpid(), SIGHUP);
		break;
	case ACTION_TYPE_SHOW_MENU:
		show_menu(view, ctx,
			action.get_str("menu", NULL).c(),
			action.get_bool("atCursor", true),
			action.get_str("x.position", NULL).c(),
			action.get_str("y.position", NULL).c());
		break;
	case ACTION_TYPE_TOGGLE_MAXIMIZE:
		if (view) {
			auto axis = (view_axis)action.get_int("direction",
				VIEW_AXIS_BOTH);
			view_toggle_maximize(view, axis);
		}
		break;
	case ACTION_TYPE_MAXIMIZE:
		if (view) {
			auto axis = (view_axis)action.get_int("direction",
				VIEW_AXIS_BOTH);
			view_maximize(view, axis);
		}
		break;
	case ACTION_TYPE_UNMAXIMIZE:
		if (view) {
			auto axis = (view_axis)action.get_int("direction",
				VIEW_AXIS_BOTH);
			view_maximize(view, view->maximized & ~axis);
		}
		break;
	case ACTION_TYPE_TOGGLE_FULLSCREEN:
		if (view) {
			view_toggle_fullscreen(view);
		}
		break;
	case ACTION_TYPE_SET_DECORATIONS:
		if (view) {
			auto mode = (lab_ssd_mode)action.get_int("decorations",
				LAB_SSD_MODE_FULL);
			bool force_ssd = action.get_bool("forceSSD", false);
			view_set_decorations(view, mode, force_ssd);
		}
		break;
	case ACTION_TYPE_TOGGLE_DECORATIONS:
		if (view) {
			view_toggle_decorations(view);
		}
		break;
	case ACTION_TYPE_TOGGLE_ALWAYS_ON_TOP:
		if (view) {
			view_toggle_always_on_top(view);
		}
		break;
	case ACTION_TYPE_TOGGLE_ALWAYS_ON_BOTTOM:
		if (view) {
			view_toggle_always_on_bottom(view);
		}
		break;
	case ACTION_TYPE_TOGGLE_OMNIPRESENT:
		if (view) {
			view_toggle_visible_on_all_workspaces(view);
		}
		break;
	case ACTION_TYPE_FOCUS:
		if (view) {
			desktop_focus_view(view, /*raise*/ false);
		}
		break;
	case ACTION_TYPE_UNFOCUS:
		seat_focus_surface(NULL);
		break;
	case ACTION_TYPE_ICONIFY:
		if (view) {
			view_minimize(view, true);
		}
		break;
	case ACTION_TYPE_MOVE:
		if (view) {
			interactive_begin(view, LAB_INPUT_STATE_MOVE,
				LAB_EDGE_NONE);
		}
		break;
	case ACTION_TYPE_RAISE:
		if (view) {
			view_move_to_front(view);
		}
		break;
	case ACTION_TYPE_LOWER:
		if (view) {
			view_move_to_back(view);
		}
		break;
	case ACTION_TYPE_RESIZE:
		if (view) {
			/*
			 * If a direction was specified in the config, honour it.
			 * Otherwise, fall back to determining the resize edges from
			 * the current cursor position (existing behaviour).
			 */
			enum lab_edge resize_edges =
				action.get_int("direction", LAB_EDGE_NONE);
			if (resize_edges == LAB_EDGE_NONE) {
				resize_edges = cursor_get_resize_edges(
					g_seat.cursor, ctx);
			}
			interactive_begin(view, LAB_INPUT_STATE_RESIZE,
				resize_edges);
		}
		break;
	case ACTION_TYPE_RESIZE_RELATIVE:
		if (view) {
			int left = action.get_int("left", 0);
			int right = action.get_int("right", 0);
			int top = action.get_int("top", 0);
			int bottom = action.get_int("bottom", 0);
			view_resize_relative(view, left, right, top, bottom);
		}
		break;
	case ACTION_TYPE_MOVETO:
		if (view) {
			int x = action.get_int("x", 0);
			int y = action.get_int("y", 0);
			struct border margin = ssd_thickness(view);
			view_move(view, x + margin.left, y + margin.top);
		}
		break;
	case ACTION_TYPE_RESIZETO:
		if (view) {
			int width = action.get_int("width", 0);
			int height = action.get_int("height", 0);

			/*
			 * To support only setting one of width/height
			 * in <action name="ResizeTo" width="" height=""/>
			 * we fall back to current dimension when unset.
			 */
			struct wlr_box box = {
				.x = view->pending.x,
				.y = view->pending.y,
				.width = width ? : view->pending.width,
				.height = height ? : view->pending.height,
			};
			view_set_shade(view, false);
			view_move_resize(view, box);
		}
		break;
	case ACTION_TYPE_MOVE_RELATIVE:
		if (view) {
			int x = action.get_int("x", 0);
			int y = action.get_int("y", 0);
			view_move_relative(view, x, y);
		}
		break;
	case ACTION_TYPE_MOVETO_CURSOR:
		wlr_log(WLR_ERROR,
			"Action MoveToCursor is deprecated. To ensure your config works in future labwc "
			"releases, please use <action name=\"AutoPlace\" policy=\"cursor\">");
		if (view) {
			view_move_to_cursor(view);
		}
		break;
	case ACTION_TYPE_SEND_TO_DESKTOP:
		if (!view) {
			break;
		}
		[[fallthrough]];
	case ACTION_TYPE_GO_TO_DESKTOP: {
		bool follow = true;
		bool wrap = action.get_bool("wrap", true);
		lab_str to = action.get_str("to", NULL);
		/*
		 * `to` is always != NULL here because otherwise we would have
		 * removed the action during the initial parsing step as it is
		 * a required argument for both SendToDesktop and GoToDesktop.
		 */
		ASSERT_PTR(g_server.workspaces.current, current);
		struct workspace *target_workspace =
			workspaces_find(current, to.c(), wrap);
		if (action.type == ACTION_TYPE_GO_TO_DESKTOP) {
			bool toggle = action.get_bool("toggle", false);
			if (target_workspace == current && toggle) {
				target_workspace = g_server.workspaces.last.get();
			}
		}
		if (!target_workspace) {
			break;
		}
		if (action.type == ACTION_TYPE_SEND_TO_DESKTOP) {
			view_move_to_workspace(view, target_workspace);
			follow = action.get_bool("follow", true);

			/* Ensure that the focus is not on another desktop */
			if (!follow && g_server.active_view == view) {
				desktop_focus_topmost_view();
			}
		}
		if (follow) {
			workspaces_switch_to(target_workspace,
				/*update_focus*/ true);
		}
		break;
	}
	case ACTION_TYPE_MOVE_TO_OUTPUT: {
		if (!view) {
			break;
		}
		struct output *target_output =
			get_target_output(view->output, action);
		if (target_output) {
			view_move_to_output(view, target_output);
		}
		break;
	}
	case ACTION_TYPE_FIT_TO_OUTPUT:
		if (!view) {
			break;
		}
		view_constrain_size_to_that_of_usable_area(view);
		break;
	case ACTION_TYPE_TOGGLE_SNAP_TO_REGION:
	case ACTION_TYPE_SNAP_TO_REGION: {
		if (!view) {
			break;
		}
		struct output *output = view->output;
		if (!output) {
			break;
		}
		lab_str region_name = action.get_str("region", NULL);
		auto region = regions_from_name(region_name.c(), output);
		if (region) {
			if (action.type == ACTION_TYPE_TOGGLE_SNAP_TO_REGION
					&& view->maximized == VIEW_AXIS_NONE
					&& !view->fullscreen
					&& view_is_tiled(view)
					&& view->tiled_region == region) {
				view_set_untiled(view);
				view_apply_natural_geometry(view);
				break;
			}
			view_snap_to_region(view, region.get());
		} else {
			wlr_log(WLR_ERROR, "Invalid SnapToRegion id: '%s'",
				region_name.c());
		}
		break;
	}
	case ACTION_TYPE_UNSNAP:
		if (view && !view->fullscreen && !view_is_floating(view)) {
			view_maximize(view, VIEW_AXIS_NONE);
			view_set_untiled(view);
			view_apply_natural_geometry(view);
		}
		break;
	case ACTION_TYPE_TOGGLE_KEYBINDS:
		if (view) {
			view_toggle_keybinds(view);
		}
		break;
	case ACTION_TYPE_FOCUS_OUTPUT: {
		struct output *output = output_nearest_to_cursor();
		struct output *target_output =
			get_target_output(output, action);
		if (target_output) {
			desktop_focus_output(target_output);
		}
		break;
	}
	case ACTION_TYPE_IF: {
		/* At least one of the queries was matched or there was no query */
		if (action.get_str("message.prompt", NULL)) {
			/*
			 * We delay the selection and execution of the
			 * branch until we get a response from the user.
			 */
			action_prompt_create(view, action);
		} else if (view) {
			std::vector<::action> *actions;
			if (match_queries(view, action)) {
				actions = action.get_actionlist("then");
			} else {
				actions = action.get_actionlist("else");
			}
			if (actions) {
				actions_run(view, *actions, ctx);
			}
		}
		break;
	}
	case ACTION_TYPE_FOR_EACH: {
		std::vector<::action> *actions;
		bool matches = false;

		for (auto &item : view_list_matching(LAB_VIEW_CRITERIA_NONE)) {
			if (match_queries(&item, action)) {
				matches = true;
				actions = action.get_actionlist("then");
			} else {
				actions = action.get_actionlist("else");
			}
			if (actions) {
				actions_run(&item, *actions, ctx);
			}
		}
		if (!matches) {
			actions = action.get_actionlist("none");
			if (actions) {
				actions_run(view, *actions, NULL);
			}
		}
		break;
	}
	case ACTION_TYPE_VIRTUAL_OUTPUT_ADD: {
		/* TODO: rename this argument to "outputName" */
		lab_str output_name = action.get_str("output_name", NULL);
		output_virtual_add(output_name.c(),
			/*store_wlr_output*/ NULL);
		break;
	}
	case ACTION_TYPE_VIRTUAL_OUTPUT_REMOVE: {
		/* TODO: rename this argument to "outputName" */
		lab_str output_name = action.get_str("output_name", NULL);
		output_virtual_remove(output_name.c());
		break;
	}
	case ACTION_TYPE_AUTO_PLACE:
		if (view) {
			auto policy = (lab_placement_policy)
				action.get_int("policy", LAB_PLACE_AUTOMATIC);
			view_place_by_policy(view,
				/* allow_cursor */ true, policy);
		}
		break;
	case ACTION_TYPE_TOGGLE_TEARING:
		if (view) {
			switch (view->force_tearing) {
			case LAB_STATE_UNSPECIFIED:
				view->force_tearing =
					output_get_tearing_allowance(view->output)
						? LAB_STATE_DISABLED : LAB_STATE_ENABLED;
				break;
			case LAB_STATE_DISABLED:
				view->force_tearing = LAB_STATE_ENABLED;
				break;
			case LAB_STATE_ENABLED:
				view->force_tearing = LAB_STATE_DISABLED;
				break;
			}
			wlr_log(WLR_ERROR, "force tearing %sabled",
				view->force_tearing == LAB_STATE_ENABLED
					? "en" : "dis");
		}
		break;
	case ACTION_TYPE_TOGGLE_SHADE:
		if (view) {
			view_set_shade(view, !view->shaded);
		}
		break;
	case ACTION_TYPE_SHADE:
		if (view) {
			view_set_shade(view, true);
		}
		break;
	case ACTION_TYPE_UNSHADE:
		if (view) {
			view_set_shade(view, false);
		}
		break;
	case ACTION_TYPE_ENABLE_SCROLL_WHEEL_EMULATION:
		g_seat.cursor_scroll_wheel_emulation = true;
		break;
	case ACTION_TYPE_DISABLE_SCROLL_WHEEL_EMULATION:
		g_seat.cursor_scroll_wheel_emulation = false;
		break;
	case ACTION_TYPE_TOGGLE_SCROLL_WHEEL_EMULATION:
		g_seat.cursor_scroll_wheel_emulation =
			!g_seat.cursor_scroll_wheel_emulation;
		break;
	case ACTION_TYPE_ENABLE_TABLET_MOUSE_EMULATION:
		rc.tablet.force_mouse_emulation = true;
		break;
	case ACTION_TYPE_DISABLE_TABLET_MOUSE_EMULATION:
		rc.tablet.force_mouse_emulation = false;
		break;
	case ACTION_TYPE_TOGGLE_TABLET_MOUSE_EMULATION:
		rc.tablet.force_mouse_emulation = !rc.tablet.force_mouse_emulation;
		break;
	case ACTION_TYPE_TOGGLE_MAGNIFY:
		magnifier_toggle();
		break;
	case ACTION_TYPE_ZOOM_IN:
		magnifier_set_scale(MAGNIFY_INCREASE);
		break;
	case ACTION_TYPE_ZOOM_OUT:
		magnifier_set_scale(MAGNIFY_DECREASE);
		break;
	case ACTION_TYPE_WARP_CURSOR: {
		lab_str to = action.get_str("to", "output");
		lab_str x = action.get_str("x", "center");
		lab_str y = action.get_str("y", "center");
		warp_cursor(view, to.c(), x.c(), y.c());
		break;
	}
	case ACTION_TYPE_HIDE_CURSOR:
		cursor_set_visible(false);
		break;
	case ACTION_TYPE_INVALID:
		wlr_log(WLR_ERROR, "Not executing unknown action");
		break;
	default:
		/*
		 * If we get here it must be a BUG caused most likely by
		 * action_names and action_type being out of sync or by
		 * adding a new action without installing a handler here.
		 */
		wlr_log(WLR_ERROR,
			"Not executing invalid action (%u)"
			" This is a BUG. Please report.", action.type);
	}
}

void
actions_run(struct view *activator, std::vector<action> &actions,
		struct cursor_context *cursor_ctx)
{
	if (actions.empty()) {
		wlr_log(WLR_ERROR, "empty actions");
		return;
	}

	/* This cancels any pending on-release keybinds */
	keyboard_reset_current_keybind();

	struct cursor_context ctx = {0};
	if (cursor_ctx) {
		ctx = *cursor_ctx;
	}

	for (auto &action : actions) {
		if (g_server.input_mode == LAB_INPUT_STATE_CYCLE
				&& action.type != ACTION_TYPE_NEXT_WINDOW
				&& action.type != ACTION_TYPE_PREVIOUS_WINDOW) {
			wlr_log(WLR_INFO, "Only NextWindow or PreviousWindow "
				"actions are accepted while window switching.");
			continue;
		}

		wlr_log(WLR_DEBUG, "Handling action %u: %s", action.type,
			action_names[action.type]);

		/*
		 * Refetch view because it may have been changed due to the
		 * previous action
		 */
		struct view *view = view_for_action(activator, action, &ctx);

		run_action(view, action, &ctx);
	}
}
