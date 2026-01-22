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
#include <wlr/util/log.h>
#include "common/buf.h"
#include "common/list.h"
#include "common/mem.h"
#include "common/parse-bool.h"
#include "common/spawn.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "cycle.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "menu/menu.h"
#include "output.h"
#include "ssd.h"
#include "theme.h"
#include "view.h"

enum action_arg_type {
	LAB_ACTION_ARG_STR = 0,
	LAB_ACTION_ARG_BOOL,
	LAB_ACTION_ARG_INT,
};

struct action_arg {
	struct wl_list link;        /* struct action.args */

	char *key;                  /* May be NULL if there is just one arg */
	enum action_arg_type type;
};

struct action_arg_str {
	struct action_arg base;
	char *value;
};

struct action_arg_bool {
	struct action_arg base;
	bool value;
};

struct action_arg_int {
	struct action_arg base;
	int value;
};

enum action_type {
	ACTION_TYPE_INVALID = 0,
	ACTION_TYPE_NONE,
	ACTION_TYPE_CLOSE,
	ACTION_TYPE_EXECUTE,
	ACTION_TYPE_EXIT,
	ACTION_TYPE_SNAP_TO_EDGE,
	ACTION_TYPE_NEXT_WINDOW,
	ACTION_TYPE_PREVIOUS_WINDOW,
	ACTION_TYPE_RECONFIGURE,
	ACTION_TYPE_SHOW_MENU,
	ACTION_TYPE_TOGGLE_MAXIMIZE,
	ACTION_TYPE_MAXIMIZE,
	ACTION_TYPE_UNMAXIMIZE,
	ACTION_TYPE_TOGGLE_FULLSCREEN,
	ACTION_TYPE_TOGGLE_ALWAYS_ON_TOP,
	ACTION_TYPE_FOCUS,
	ACTION_TYPE_ICONIFY,
	ACTION_TYPE_MOVE,
	ACTION_TYPE_RAISE,
	ACTION_TYPE_RESIZE,
	ACTION_TYPE_TOGGLE_KEYBINDS,
};

const char *action_names[] = {
	"INVALID",
	"None",
	"Close",
	"Execute",
	"Exit",
	"SnapToEdge",
	"NextWindow",
	"PreviousWindow",
	"Reconfigure",
	"ShowMenu",
	"ToggleMaximize",
	"Maximize",
	"UnMaximize",
	"ToggleFullscreen",
	"ToggleAlwaysOnTop",
	"Focus",
	"Iconify",
	"Move",
	"Raise",
	"Resize",
	"ToggleKeybinds",
	NULL
};

void
action_arg_add_str(struct action *action, const char *key, const char *value)
{
	assert(action);
	assert(key);
	assert(value && "Tried to add NULL action string argument");
	struct action_arg_str *arg = znew(*arg);
	arg->base.type = LAB_ACTION_ARG_STR;
	arg->base.key = xstrdup(key);
	arg->value = xstrdup(value);
	wl_list_append(&action->args, &arg->base.link);
}

static void
action_arg_add_bool(struct action *action, const char *key, bool value)
{
	assert(action);
	assert(key);
	struct action_arg_bool *arg = znew(*arg);
	arg->base.type = LAB_ACTION_ARG_BOOL;
	arg->base.key = xstrdup(key);
	arg->value = value;
	wl_list_append(&action->args, &arg->base.link);
}

static void
action_arg_add_int(struct action *action, const char *key, int value)
{
	assert(action);
	assert(key);
	struct action_arg_int *arg = znew(*arg);
	arg->base.type = LAB_ACTION_ARG_INT;
	arg->base.key = xstrdup(key);
	arg->value = value;
	wl_list_append(&action->args, &arg->base.link);
}

static void *
action_get_arg(struct action *action, const char *key, enum action_arg_type type)
{
	assert(action);
	assert(key);
	struct action_arg *arg;
	wl_list_for_each(arg, &action->args, link) {
		if (!strcasecmp(key, arg->key) && arg->type == type) {
			return arg;
		}
	}
	return NULL;
}

const char *
action_get_str(struct action *action, const char *key, const char *default_value)
{
	struct action_arg_str *arg = action_get_arg(action, key, LAB_ACTION_ARG_STR);
	return arg ? arg->value : default_value;
}

static bool
action_get_bool(struct action *action, const char *key, bool default_value)
{
	struct action_arg_bool *arg = action_get_arg(action, key, LAB_ACTION_ARG_BOOL);
	return arg ? arg->value : default_value;
}

static int
action_get_int(struct action *action, const char *key, int default_value)
{
	struct action_arg_int *arg = action_get_arg(action, key, LAB_ACTION_ARG_INT);
	return arg ? arg->value : default_value;
}

void
action_arg_from_xml_node(struct action *action, const char *nodename, const char *content)
{
	assert(action);

	char *argument = xstrdup(nodename);
	string_truncate_at_pattern(argument, ".action");

	switch (action->type) {
	case ACTION_TYPE_EXECUTE:
		/*
		 * <action name="Execute"> with an <execute> child is
		 * deprecated, but we support it anyway for backward
		 * compatibility with old openbox-menu generators
		 */
		if (!strcmp(argument, "command") || !strcmp(argument, "execute")) {
			action_arg_add_str(action, "command", content);
			goto cleanup;
		}
		break;
	case ACTION_TYPE_SNAP_TO_EDGE:
		if (!strcmp(argument, "direction")) {
			enum lab_edge edge = lab_edge_parse(content,
				/*tiled*/ true, /*any*/ false);
			if (edge == LAB_EDGE_NONE) {
				wlr_log(WLR_ERROR, "Invalid argument for action %s: '%s' (%s)",
					action_names[action->type], argument, content);
			} else {
				action_arg_add_int(action, argument, edge);
			}
			goto cleanup;
		}
		if (!strcasecmp(argument, "combine")) {
			action_arg_add_bool(action, argument, parse_bool(content, false));
			goto cleanup;
		}
		break;
	case ACTION_TYPE_SHOW_MENU:
		if (!strcmp(argument, "menu")) {
			action_arg_add_str(action, argument, content);
			goto cleanup;
		}
		if (!strcasecmp(argument, "atCursor")) {
			action_arg_add_bool(action, argument, parse_bool(content, true));
			goto cleanup;
		}
		if (!strcasecmp(argument, "x.position")) {
			action_arg_add_str(action, argument, content);
			goto cleanup;
		}
		if (!strcasecmp(argument, "y.position")) {
			action_arg_add_str(action, argument, content);
			goto cleanup;
		}
		break;
	case ACTION_TYPE_TOGGLE_MAXIMIZE:
	case ACTION_TYPE_MAXIMIZE:
	case ACTION_TYPE_UNMAXIMIZE:
		if (!strcmp(argument, "direction")) {
			enum view_axis axis = view_axis_parse(content);
			if (axis == VIEW_AXIS_NONE) {
				wlr_log(WLR_ERROR, "Invalid argument for action %s: '%s' (%s)",
					action_names[action->type], argument, content);
			} else {
				action_arg_add_int(action, argument, axis);
			}
			goto cleanup;
		}
		break;
	case ACTION_TYPE_RESIZE:
		if (!strcmp(argument, "direction")) {
			enum lab_edge edge = lab_edge_parse(content,
				/*tiled*/ true, /*any*/ false);
			if (edge == LAB_EDGE_NONE || edge == LAB_EDGE_CENTER) {
				wlr_log(WLR_ERROR,
					"Invalid argument for action %s: '%s' (%s)",
					action_names[action->type], argument, content);
			} else {
				action_arg_add_int(action, argument, edge);
			}
			goto cleanup;
		}
		break;
	}

	wlr_log(WLR_ERROR, "Invalid argument for action %s: '%s'",
		action_names[action->type], argument);

cleanup:
	free(argument);
}

static enum action_type
action_type_from_str(const char *action_name)
{
	for (size_t i = 1; action_names[i]; i++) {
		if (!strcasecmp(action_name, action_names[i])) {
			return i;
		}
	}
	wlr_log(WLR_ERROR, "Invalid action: %s", action_name);
	return ACTION_TYPE_INVALID;
}

struct action *
action_create(const char *action_name)
{
	if (!action_name) {
		wlr_log(WLR_ERROR, "action name not specified");
		return NULL;
	}

	enum action_type action_type = action_type_from_str(action_name);
	if (action_type == ACTION_TYPE_NONE) {
		return NULL;
	}

	struct action *action = znew(*action);
	action->type = action_type;
	wl_list_init(&action->args);
	return action;
}

bool
actions_contain_toggle_keybinds(struct wl_list *action_list)
{
	struct action *action;
	wl_list_for_each(action, action_list, link) {
		if (action->type == ACTION_TYPE_TOGGLE_KEYBINDS) {
			return true;
		}
	}
	return false;
}

/* Checks for *required* arguments */
bool
action_is_valid(struct action *action)
{
	const char *arg_name = NULL;
	enum action_arg_type arg_type = LAB_ACTION_ARG_STR;

	switch (action->type) {
	case ACTION_TYPE_EXECUTE:
		arg_name = "command";
		break;
	case ACTION_TYPE_SNAP_TO_EDGE:
		arg_name = "direction";
		arg_type = LAB_ACTION_ARG_INT;
		break;
	case ACTION_TYPE_SHOW_MENU:
		arg_name = "menu";
		break;
	default:
		/* No arguments required */
		return true;
	}

	if (action_get_arg(action, arg_name, arg_type)) {
		return true;
	}

	wlr_log(WLR_ERROR, "Missing required argument for %s: %s",
		action_names[action->type], arg_name);
	return false;
}

bool
action_is_show_menu(struct action *action)
{
	return action->type == ACTION_TYPE_SHOW_MENU;
}

void
action_free(struct action *action)
{
	/* Free args */
	struct action_arg *arg, *arg_tmp;
	wl_list_for_each_safe(arg, arg_tmp, &action->args, link) {
		wl_list_remove(&arg->link);
		zfree(arg->key);
		if (arg->type == LAB_ACTION_ARG_STR) {
			struct action_arg_str *str_arg = (struct action_arg_str *)arg;
			zfree(str_arg->value);
		}
		zfree(arg);
	}
	zfree(action);
}

void
action_list_free(struct wl_list *action_list)
{
	struct action *action, *action_tmp;
	wl_list_for_each_safe(action, action_tmp, action_list, link) {
		wl_list_remove(&action->link);
		action_free(action);
	}
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
		y = view->st->current.y;
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
	if (pos_x && pos_y) {
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
view_for_action(struct view *activator, struct action *action,
		struct cursor_context *ctx)
{
	/* View is explicitly specified for mousebinds */
	if (activator) {
		return activator;
	}

	/* Select view based on action type for keybinds */
	switch (action->type) {
	case ACTION_TYPE_FOCUS:
	case ACTION_TYPE_MOVE:
	case ACTION_TYPE_RESIZE: {
		*ctx = get_cursor_context();
		return ctx->view;
	}
	default:
		return view_get_active();
	}
}

static void
run_action(struct view *view, struct action *action, struct cursor_context *ctx)
{
	switch (action->type) {
	case ACTION_TYPE_CLOSE:
		if (view) {
			view_close(view->id);
		}
		break;
	case ACTION_TYPE_EXECUTE: {
		struct buf cmd = BUF_INIT;
		buf_add(&cmd, action_get_str(action, "command", NULL));
		buf_expand_tilde(&cmd);
		spawn_async_no_shell(cmd.data);
		buf_reset(&cmd);
		break;
	}
	case ACTION_TYPE_EXIT:
		wl_display_terminate(g_server.wl_display);
		break;
	case ACTION_TYPE_SNAP_TO_EDGE:
		if (view) {
			/* Config parsing makes sure that direction is a valid direction */
			enum lab_edge edge = action_get_int(action, "direction", 0);
			view_tile(view->id, edge);
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
			action_get_str(action, "menu", NULL),
			action_get_bool(action, "atCursor", true),
			action_get_str(action, "x.position", NULL),
			action_get_str(action, "y.position", NULL));
		break;
	case ACTION_TYPE_TOGGLE_MAXIMIZE:
		if (view) {
			enum view_axis axis = action_get_int(action,
				"direction", VIEW_AXIS_BOTH);
			view_toggle_maximize(view, axis);
		}
		break;
	case ACTION_TYPE_MAXIMIZE:
		if (view) {
			enum view_axis axis = action_get_int(action,
				"direction", VIEW_AXIS_BOTH);
			view_maximize(view->id, axis);
		}
		break;
	case ACTION_TYPE_UNMAXIMIZE:
		if (view) {
			enum view_axis axis = action_get_int(action,
				"direction", VIEW_AXIS_BOTH);
			view_maximize(view->id, view->st->maximized & ~axis);
		}
		break;
	case ACTION_TYPE_TOGGLE_FULLSCREEN:
		if (view) {
			view_toggle_fullscreen(view);
		}
		break;
	case ACTION_TYPE_TOGGLE_ALWAYS_ON_TOP:
		if (view) {
			view_toggle_always_on_top(view);
		}
		break;
	case ACTION_TYPE_FOCUS:
		if (view) {
			view_focus(view->id, /*raise*/ false);
		}
		break;
	case ACTION_TYPE_ICONIFY:
		if (view) {
			view_minimize(view->id, true);
		}
		break;
	case ACTION_TYPE_MOVE:
		if (view) {
			/*
			 * If triggered by mousebind, grab context was already
			 * set by button press handling. For keybind-triggered
			 * Move, set it now from current cursor position.
			 */
			if (view != g_seat.pressed.ctx.view) {
				interactive_set_grab_context(ctx);
			}
			interactive_begin(view, LAB_INPUT_STATE_MOVE,
				LAB_EDGE_NONE);
		}
		break;
	case ACTION_TYPE_RAISE:
		if (view) {
			view_raise(view->id);
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
				action_get_int(action, "direction", LAB_EDGE_NONE);
			/*
			 * If triggered by mousebind, grab context was already
			 * set by button press handling. For keybind-triggered
			 * Resize, set it now from current cursor position.
			 */
			if (view != g_seat.pressed.ctx.view) {
				interactive_set_grab_context(ctx);
			}
			interactive_begin(view, LAB_INPUT_STATE_RESIZE,
				resize_edges);
		}
		break;
	case ACTION_TYPE_TOGGLE_KEYBINDS:
		if (view) {
			view_toggle_keybinds(view);
		}
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
			" This is a BUG. Please report.", action->type);
	}
}

void
actions_run(struct view *activator, struct wl_list *actions,
		struct cursor_context *cursor_ctx)
{
	if (!actions) {
		wlr_log(WLR_ERROR, "empty actions");
		return;
	}

	/* This cancels any pending on-release keybinds */
	keyboard_reset_current_keybind();

	struct cursor_context ctx = {0};
	if (cursor_ctx) {
		ctx = *cursor_ctx;
	}

	struct action *action;
	wl_list_for_each(action, actions, link) {
		if (g_server.input_mode == LAB_INPUT_STATE_CYCLE
				&& action->type != ACTION_TYPE_NEXT_WINDOW
				&& action->type != ACTION_TYPE_PREVIOUS_WINDOW) {
			wlr_log(WLR_INFO, "Only NextWindow or PreviousWindow "
				"actions are accepted while window switching.");
			continue;
		}

		wlr_log(WLR_DEBUG, "Handling action %u: %s", action->type,
			action_names[action->type]);

		/*
		 * Refetch view because it may have been changed due to the
		 * previous action
		 */
		struct view *view = view_for_action(activator, action, &ctx);

		run_action(view, action, &ctx);
	}
}
