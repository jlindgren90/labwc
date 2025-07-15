// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/mousebind.h"
#include <assert.h>
#include <linux/input-event-codes.h>
#include <strings.h>
#include <wlr/util/log.h>
#include "common/list.h"
#include "config/keybind.h"
#include "config/rcxml.h"

uint32_t
mousebind_button_from_str(const char *str, uint32_t *modifiers)
{
	assert(str);

	if (modifiers) {
		*modifiers = 0;
		while (strlen(str) >= 2 && str[1] == '-') {
			char modname[2] = {str[0], 0};
			uint32_t parsed_modifier = parse_modifier(modname);
			if (!parsed_modifier) {
				goto invalid;
			}
			*modifiers |= parsed_modifier;
			str += 2;
		}
	}

	if (!strcasecmp(str, "Left")) {
		return BTN_LEFT;
	} else if (!strcasecmp(str, "Right")) {
		return BTN_RIGHT;
	} else if (!strcasecmp(str, "Middle")) {
		return BTN_MIDDLE;
	} else if (!strcasecmp(str, "Side")) {
		return BTN_SIDE;
	} else if (!strcasecmp(str, "Extra")) {
		return BTN_EXTRA;
	} else if (!strcasecmp(str, "Forward")) {
		return BTN_FORWARD;
	} else if (!strcasecmp(str, "Back")) {
		return BTN_BACK;
	} else if (!strcasecmp(str, "Task")) {
		return BTN_TASK;
	}
invalid:
	wlr_log(WLR_ERROR, "unknown button (%s)", str);
	return UINT32_MAX;
}

enum direction
mousebind_direction_from_str(const char *str, uint32_t *modifiers)
{
	assert(str);

	if (modifiers) {
		*modifiers = 0;
		while (strlen(str) >= 2 && str[1] == '-') {
			char modname[2] = {str[0], 0};
			uint32_t parsed_modifier = parse_modifier(modname);
			if (!parsed_modifier) {
				goto invalid;
			}
			*modifiers |= parsed_modifier;
			str += 2;
		}
	}

	if (!strcasecmp(str, "Left")) {
		return LAB_DIRECTION_LEFT;
	} else if (!strcasecmp(str, "Right")) {
		return LAB_DIRECTION_RIGHT;
	} else if (!strcasecmp(str, "Up")) {
		return LAB_DIRECTION_UP;
	} else if (!strcasecmp(str, "Down")) {
		return LAB_DIRECTION_DOWN;
	}
invalid:
	wlr_log(WLR_ERROR, "unknown direction (%s)", str);
	return LAB_DIRECTION_INVALID;
}

enum mouse_event
mousebind_event_from_str(const char *str)
{
	assert(str);
	if (!strcasecmp(str, "doubleclick")) {
		return MOUSE_ACTION_DOUBLECLICK;
	} else if (!strcasecmp(str, "click")) {
		return MOUSE_ACTION_CLICK;
	} else if (!strcasecmp(str, "press")) {
		return MOUSE_ACTION_PRESS;
	} else if (!strcasecmp(str, "release")) {
		return MOUSE_ACTION_RELEASE;
	} else if (!strcasecmp(str, "drag")) {
		return MOUSE_ACTION_DRAG;
	} else if (!strcasecmp(str, "scroll")) {
		return MOUSE_ACTION_SCROLL;
	}
	wlr_log(WLR_ERROR, "unknown mouse action (%s)", str);
	return MOUSE_ACTION_NONE;
}

static enum lab_node_type
context_from_str(const char *str)
{
	if (!strcasecmp(str, "Close")) {
		return LAB_NODE_BUTTON_CLOSE;
	} else if (!strcasecmp(str, "Maximize")) {
		return LAB_NODE_BUTTON_MAXIMIZE;
	} else if (!strcasecmp(str, "Iconify")) {
		return LAB_NODE_BUTTON_ICONIFY;
	} else if (!strcasecmp(str, "WindowMenu")) {
		return LAB_NODE_BUTTON_WINDOW_MENU;
	} else if (!strcasecmp(str, "Icon")) {
		return LAB_NODE_BUTTON_WINDOW_ICON;
	} else if (!strcasecmp(str, "Shade")) {
		return LAB_NODE_BUTTON_SHADE;
	} else if (!strcasecmp(str, "AllDesktops")) {
		return LAB_NODE_BUTTON_OMNIPRESENT;
	} else if (!strcasecmp(str, "Titlebar")) {
		return LAB_NODE_TITLEBAR;
	} else if (!strcasecmp(str, "Title")) {
		return LAB_NODE_TITLE;
	} else if (!strcasecmp(str, "TLCorner")) {
		return LAB_NODE_CORNER_TOP_LEFT;
	} else if (!strcasecmp(str, "TRCorner")) {
		return LAB_NODE_CORNER_TOP_RIGHT;
	} else if (!strcasecmp(str, "BRCorner")) {
		return LAB_NODE_CORNER_BOTTOM_RIGHT;
	} else if (!strcasecmp(str, "BLCorner")) {
		return LAB_NODE_CORNER_BOTTOM_LEFT;
	} else if (!strcasecmp(str, "Top")) {
		return LAB_NODE_EDGE_TOP;
	} else if (!strcasecmp(str, "Right")) {
		return LAB_NODE_EDGE_RIGHT;
	} else if (!strcasecmp(str, "Bottom")) {
		return LAB_NODE_EDGE_BOTTOM;
	} else if (!strcasecmp(str, "Left")) {
		return LAB_NODE_EDGE_LEFT;
	} else if (!strcasecmp(str, "Frame")) {
		return LAB_NODE_FRAME;
	} else if (!strcasecmp(str, "Client")) {
		return LAB_NODE_CLIENT;
	} else if (!strcasecmp(str, "Desktop")) {
		return LAB_NODE_ROOT;
	} else if (!strcasecmp(str, "Root")) {
		return LAB_NODE_ROOT;
	} else if (!strcasecmp(str, "All")) {
		return LAB_NODE_ALL;
	}
	wlr_log(WLR_ERROR, "unknown mouse context (%s)", str);
	return LAB_NODE_NONE;
}

bool
mousebind_the_same(struct mousebind *a, struct mousebind *b)
{
	assert(a && b);
	return a->context == b->context
		&& a->button == b->button
		&& a->direction == b->direction
		&& a->mouse_event == b->mouse_event
		&& a->modifiers == b->modifiers;
}

struct mousebind *
mousebind_create(const char *context)
{
	if (!context) {
		wlr_log(WLR_ERROR, "mousebind context not specified");
		return NULL;
	}
	auto m = new mousebind{};
	m->context = context_from_str(context);
	if (m->context != LAB_NODE_NONE) {
		wl_list_append(&rc.mousebinds, &m->link);
	}
	return m;
}
