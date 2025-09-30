// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/rcxml.h"
#include <assert.h>
#include <glib.h>
#include <libxml/parser.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fstream>
#include <sstream>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "action.h"
#include "common/dir.h"
#include "common/macros.h"
#include "common/nodename.h"
#include "common/parse-bool.h"
#include "common/parse-double.h"
#include "common/string-helpers.h"
#include "common/xml.h"
#include "config/default-bindings.h"
#include "config/keybind.h"
#include "config/libinput.h"
#include "config/mousebind.h"
#include "config/tablet.h"
#include "config/tablet-tool.h"
#include "config/touch.h"
#include "osd.h"
#include "regions.h"
#include "ssd.h"
#include "translate.h"
#include "view.h"
#include "window-rules.h"

/* for backward compatibility of <mouse><scrollFactor> */
static double mouse_scroll_factor = -1;

enum font_place {
	FONT_PLACE_NONE = 0,
	FONT_PLACE_UNKNOWN,
	FONT_PLACE_ACTIVEWINDOW,
	FONT_PLACE_INACTIVEWINDOW,
	FONT_PLACE_MENUHEADER,
	FONT_PLACE_MENUITEM,
	FONT_PLACE_OSD,
	/* TODO: Add all places based on Openbox's rc.xml */
};

static void load_default_key_bindings(void);
static void load_default_mouse_bindings(void);

static enum lab_window_type
parse_window_type(const char *type)
{
	if (!type) {
		return LAB_WINDOW_TYPE_INVALID;
	}
	if (!strcasecmp(type, "desktop")) {
		return LAB_WINDOW_TYPE_DESKTOP;
	} else if (!strcasecmp(type, "dock")) {
		return LAB_WINDOW_TYPE_DOCK;
	} else if (!strcasecmp(type, "toolbar")) {
		return LAB_WINDOW_TYPE_TOOLBAR;
	} else if (!strcasecmp(type, "menu")) {
		return LAB_WINDOW_TYPE_MENU;
	} else if (!strcasecmp(type, "utility")) {
		return LAB_WINDOW_TYPE_UTILITY;
	} else if (!strcasecmp(type, "splash")) {
		return LAB_WINDOW_TYPE_SPLASH;
	} else if (!strcasecmp(type, "dialog")) {
		return LAB_WINDOW_TYPE_DIALOG;
	} else if (!strcasecmp(type, "dropdown_menu")) {
		return LAB_WINDOW_TYPE_DROPDOWN_MENU;
	} else if (!strcasecmp(type, "popup_menu")) {
		return LAB_WINDOW_TYPE_POPUP_MENU;
	} else if (!strcasecmp(type, "tooltip")) {
		return LAB_WINDOW_TYPE_TOOLTIP;
	} else if (!strcasecmp(type, "notification")) {
		return LAB_WINDOW_TYPE_NOTIFICATION;
	} else if (!strcasecmp(type, "combo")) {
		return LAB_WINDOW_TYPE_COMBO;
	} else if (!strcasecmp(type, "dnd")) {
		return LAB_WINDOW_TYPE_DND;
	} else if (!strcasecmp(type, "normal")) {
		return LAB_WINDOW_TYPE_NORMAL;
	} else {
		return LAB_WINDOW_TYPE_INVALID;
	}
}

/*
 * Openbox/labwc comparison
 *
 * Instead of openbox's <titleLayout>WLIMC</title> we use
 *
 *     <titlebar>
 *       <layout>menu:iconfiy,max,close</layout>
 *       <showTitle>yes|no</showTitle>
 *     </titlebar>
 *
 * ...using the icon names (like iconify.xbm) without the file extension for the
 * identifier.
 *
 * labwc        openbox     description
 * -----        -------     -----------
 * menu         W           Open window menu (client-menu)
 * iconfiy      I           Iconify (aka minimize)
 * max          M           Maximize toggle
 * close        C           Close
 * shade        S           Shade toggle
 * desk         D           All-desktops toggle (aka omnipresent)
 */
static void
fill_section(const char *content, enum lab_node_type *buttons, int *count,
		uint32_t *found_buttons /* bitmask */)
{
	gchar **identifiers = g_strsplit(content, ",", -1);
	for (size_t i = 0; identifiers[i]; ++i) {
		char *identifier = identifiers[i];
		if (string_null_or_empty(identifier)) {
			continue;
		}
		enum lab_node_type type = LAB_NODE_NONE;
		if (!strcmp(identifier, "icon")) {
#if HAVE_LIBSFDO
			type = LAB_NODE_BUTTON_WINDOW_ICON;
#else
			wlr_log(WLR_ERROR, "libsfdo is not linked. "
				"Replacing 'icon' in titlebar layout with 'menu'.");
			type = LAB_NODE_BUTTON_WINDOW_MENU;
#endif
		} else if (!strcmp(identifier, "menu")) {
			type = LAB_NODE_BUTTON_WINDOW_MENU;
		} else if (!strcmp(identifier, "iconify")) {
			type = LAB_NODE_BUTTON_ICONIFY;
		} else if (!strcmp(identifier, "max")) {
			type = LAB_NODE_BUTTON_MAXIMIZE;
		} else if (!strcmp(identifier, "close")) {
			type = LAB_NODE_BUTTON_CLOSE;
		} else if (!strcmp(identifier, "shade")) {
			type = LAB_NODE_BUTTON_SHADE;
		} else if (!strcmp(identifier, "desk")) {
			type = LAB_NODE_BUTTON_OMNIPRESENT;
		} else {
			wlr_log(WLR_ERROR, "invalid titleLayout identifier '%s'",
				identifier);
			continue;
		}

		assert(type != LAB_NODE_NONE);

		/* We no longer need this check, but let's keep it just in case */
		if (*found_buttons & (1 << type)) {
			wlr_log(WLR_ERROR, "ignoring duplicated button type '%s'",
				identifier);
			continue;
		}

		*found_buttons |= (1 << type);

		assert(*count < TITLE_BUTTONS_MAX);
		buttons[(*count)++] = type;
	}
	g_strfreev(identifiers);
}

static void
clear_title_layout(void)
{
	rc.nr_title_buttons_left = 0;
	rc.nr_title_buttons_right = 0;
	rc.title_layout_loaded = false;
}

static void
fill_title_layout(const char *content)
{
	clear_title_layout();

	gchar **parts = g_strsplit(content, ":", -1);

	if (g_strv_length(parts) != 2) {
		wlr_log(WLR_ERROR, "<titlebar><layout> must contain one colon");
		goto err;
	}

{ /* !goto */
	uint32_t found_buttons = 0;
	fill_section(parts[0], rc.title_buttons_left,
		&rc.nr_title_buttons_left, &found_buttons);
	fill_section(parts[1], rc.title_buttons_right,
		&rc.nr_title_buttons_right, &found_buttons);

	rc.title_layout_loaded = true;
} err:
	g_strfreev(parts);
}

static void
fill_usable_area_override(xmlNode *node)
{
	rc.usable_area_overrides.push_back(usable_area_override());
	auto usable_area_override = &rc.usable_area_overrides.back();

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcmp(key, "output")) {
			usable_area_override->output = lab_str(content);
		} else if (!strcmp(key, "left")) {
			usable_area_override->margin.left = atoi(content);
		} else if (!strcmp(key, "right")) {
			usable_area_override->margin.right = atoi(content);
		} else if (!strcmp(key, "top")) {
			usable_area_override->margin.top = atoi(content);
		} else if (!strcmp(key, "bottom")) {
			usable_area_override->margin.bottom = atoi(content);
		} else {
			wlr_log(WLR_ERROR, "Unexpected data usable-area-override "
				"parser: %s=\"%s\"", key, content);
		}
	}
}

/* Does a boolean-parse but also allows 'default' */
static void
set_property(const char *str, enum property *variable)
{
	if (!str || !strcasecmp(str, "default")) {
		*variable = LAB_PROP_UNSET;
		return;
	}
	int ret = parse_bool(str, -1);
	if (ret < 0) {
		return;
	}
	*variable = ret ? LAB_PROP_TRUE : LAB_PROP_FALSE;
}

static void
fill_window_rule(xmlNode *node)
{
	rc.window_rules.push_back({.window_type = LAB_WINDOW_TYPE_INVALID});
	auto window_rule = &rc.window_rules.back();

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		/* Criteria */
		if (!strcmp(key, "identifier")) {
			window_rule->identifier = lab_str(content);
		} else if (!strcmp(key, "title")) {
			window_rule->title = lab_str(content);
		} else if (!strcmp(key, "type")) {
			window_rule->window_type = parse_window_type(content);
		} else if (!strcasecmp(key, "matchOnce")) {
			set_bool(content, &window_rule->match_once);
		} else if (!strcasecmp(key, "sandboxEngine")) {
			window_rule->sandbox_engine = lab_str(content);
		} else if (!strcasecmp(key, "sandboxAppId")) {
			window_rule->sandbox_app_id = lab_str(content);

		/* Event */
		} else if (!strcmp(key, "event")) {
			/*
			 * This is just in readiness for adding any other types of
			 * events in the future. We default to onFirstMap anyway.
			 */
			if (!strcasecmp(content, "onFirstMap")) {
				window_rule->event = LAB_WINDOW_RULE_EVENT_ON_FIRST_MAP;
			}

		/* Properties */
		} else if (!strcasecmp(key, "serverDecoration")) {
			set_property(content, &window_rule->server_decoration);
		} else if (!strcasecmp(key, "iconPriority")) {
			if (!strcasecmp(content, "client")) {
				window_rule->icon_prefer_client = LAB_PROP_TRUE;
			} else if (!strcasecmp(content, "server")) {
				window_rule->icon_prefer_client = LAB_PROP_FALSE;
			} else {
				wlr_log(WLR_ERROR,
					"Invalid value for window rule property 'iconPriority'");
			}
		} else if (!strcasecmp(key, "skipTaskbar")) {
			set_property(content, &window_rule->skip_taskbar);
		} else if (!strcasecmp(key, "skipWindowSwitcher")) {
			set_property(content, &window_rule->skip_window_switcher);
		} else if (!strcasecmp(key, "ignoreFocusRequest")) {
			set_property(content, &window_rule->ignore_focus_request);
		} else if (!strcasecmp(key, "ignoreConfigureRequest")) {
			set_property(content, &window_rule->ignore_configure_request);
		} else if (!strcasecmp(key, "fixedPosition")) {
			set_property(content, &window_rule->fixed_position);
		}
	}

	append_parsed_actions(node, window_rule->actions);
}

static void
fill_window_rules(xmlNode *node)
{
	/* TODO: make sure <windowRules> is empty here */

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "windowRule")) {
			fill_window_rule(child);
		}
	}
}

static void
fill_window_switcher_field(xmlNode *node)
{
	rc.window_switcher.fields.push_back(window_switcher_field());
	auto field = &rc.window_switcher.fields.back();

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		osd_field_arg_from_xml_node(field, key, content);
	}
}

static void
fill_window_switcher_fields(xmlNode *node)
{
	rc.window_switcher.fields.clear();

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "field")) {
			fill_window_switcher_field(child);
		}
	}
}

static void
fill_region(xmlNode *node)
{
	rc.regions.push_back(region_cfg());
	auto region = &rc.regions.back();

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "name")) {
			region->name = lab_str(content);
		} else if (strstr("xywidtheight", key)
				&& !strchr(content, '%')) {
			wlr_log(WLR_ERROR, "Removing invalid region "
				"'%s': %s='%s' misses a trailing %%",
				region->name.c(), key, content);
			rc.regions.pop_back();
			return;
		} else if (!strcmp(key, "x")) {
			region->percentage.x = atoi(content);
		} else if (!strcmp(key, "y")) {
			region->percentage.y = atoi(content);
		} else if (!strcmp(key, "width")) {
			region->percentage.width = atoi(content);
		} else if (!strcmp(key, "height")) {
			region->percentage.height = atoi(content);
		} else {
			wlr_log(WLR_ERROR, "Unexpected data in region "
				"parser: %s=\"%s\"", key, content);
		}
	}
}

static void
fill_regions(xmlNode *node)
{
	/* TODO: make sure <regions> is empty here */

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "region")) {
			fill_region(child);
		}
	}
}

static view_query
parse_query(xmlNode *node)
{
	auto query = view_query::create();
	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "identifier")) {
			query.identifier = lab_str(content);
		} else if (!strcasecmp(key, "title")) {
			query.title = lab_str(content);
		} else if (!strcmp(key, "type")) {
			query.window_type = parse_window_type(content);
		} else if (!strcasecmp(key, "sandboxEngine")) {
			query.sandbox_engine = lab_str(content);
		} else if (!strcasecmp(key, "sandboxAppId")) {
			query.sandbox_app_id = lab_str(content);
		} else if (!strcasecmp(key, "shaded")) {
			query.shaded = parse_tristate(content);
		} else if (!strcasecmp(key, "maximized")) {
			query.maximized = view_axis_parse(content);
		} else if (!strcasecmp(key, "iconified")) {
			query.iconified = parse_tristate(content);
		} else if (!strcasecmp(key, "focused")) {
			query.focused = parse_tristate(content);
		} else if (!strcasecmp(key, "omnipresent")) {
			query.omnipresent = parse_tristate(content);
		} else if (!strcasecmp(key, "tiled")) {
			query.tiled = lab_edge_parse(content,
				/*tiled*/ true, /*any*/ true);
		} else if (!strcasecmp(key, "tiled_region")) {
			query.tiled_region = lab_str(content);
		} else if (!strcasecmp(key, "desktop")) {
			query.desktop = lab_str(content);
		} else if (!strcasecmp(key, "decoration")) {
			query.decoration = ssd_mode_parse(content);
		} else if (!strcasecmp(key, "monitor")) {
			query.monitor = lab_str(content);
		}
	}
	return query;
}

static void
parse_action_args(xmlNode *node, action &action)
{
	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "query")) {
			auto querylist = action.get_querylist("query");
			if (!querylist) {
				querylist = &action.add_querylist("query");
			}
			querylist->push_back(parse_query(child));
		} else if (!strcasecmp(key, "then")) {
			auto actions = action.get_actionlist("then");
			if (!actions) {
				actions = &action.add_actionlist("then");
			}
			append_parsed_actions(child, *actions);
		} else if (!strcasecmp(key, "else")) {
			auto actions = action.get_actionlist("else");
			if (!actions) {
				actions = &action.add_actionlist("else");
			}
			append_parsed_actions(child, *actions);
		} else if (!strcasecmp(key, "none")) {
			auto actions = action.get_actionlist("none");
			if (!actions) {
				actions = &action.add_actionlist("none");
			}
			append_parsed_actions(child, *actions);
		} else if (!strcasecmp(key, "name")) {
			/* Ignore <action name=""> */
		} else if (lab_xml_node_is_leaf(child)) {
			/* Handle normal action args */
			char buffer[256];
			char *node_name = nodename(child, buffer, sizeof(buffer));
			action.add_arg_from_xml_node(node_name, content);
		} else {
			/* Handle nested args like <position><x> in ShowMenu */
			parse_action_args(child, action);
		}
	}
}

static void
append_parsed_action(xmlNode *node, std::vector<action> &actions)
{
	char name[256];
	if (lab_xml_get_string(node, "name", name, sizeof(name))) {
		auto action = action::append_new(actions, name);
		if (action) {
			parse_action_args(node, *action);
		}
	}
}

void
append_parsed_actions(xmlNode *node, std::vector<action> &actions)
{
	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (strcasecmp(key, "action")) {
			continue;
		}
		if (lab_xml_node_is_leaf(child)) {
			/*
			 * A mousebind contains two types of "action" nodes:
			 *   <mousebind button="Left" action="Click">
			 *     <action name="Close" />
			 *   </mousebind>
			 * The first node (action="Click") is skipped.
			 */
			continue;
		}
		append_parsed_action(child, actions);
	}
}

static void
fill_keybind(xmlNode *node)
{
	struct keybind *keybind = NULL;
	char keyname[256];

	if (lab_xml_get_string(node, "key", keyname, sizeof(keyname))) {
		keybind = keybind_append_new(rc.keybinds, keyname);
		if (!keybind) {
			wlr_log(WLR_ERROR, "Invalid keybind: %s", keyname);
		}
	}
	if (!keybind) {
		return;
	}

	lab_xml_get_bool(node, "onRelease", &keybind->on_release);
	lab_xml_get_bool(node, "layoutDependent", &keybind->use_syms_only);
	lab_xml_get_bool(node, "allowWhenLocked", &keybind->allow_when_locked);

	append_parsed_actions(node, keybind->actions);
}

static void
fill_mousebind(xmlNode *node, const char *context)
{
	/*
	 * Example of what we are parsing:
	 * <mousebind button="Left" action="DoubleClick">
	 *   <action name="Focus"/>
	 *   <action name="Raise"/>
	 *   <action name="ToggleMaximize"/>
	 * </mousebind>
	 */

	wlr_log(WLR_INFO, "create mousebind for %s", context);
	struct mousebind *mousebind =
		mousebind_append_new(rc.mousebinds, context);

	char buf[256];
	if (lab_xml_get_string(node, "button", buf, sizeof(buf))) {
		mousebind->button = mousebind_button_from_str(
			buf, &mousebind->modifiers);
	}
	if (lab_xml_get_string(node, "direction", buf, sizeof(buf))) {
		mousebind->direction = mousebind_direction_from_str(
			buf, &mousebind->modifiers);
	}
	if (lab_xml_get_string(node, "action", buf, sizeof(buf))) {
		/* <mousebind button="" action="EVENT"> */
		mousebind->mouse_event = mousebind_event_from_str(buf);
	}

	append_parsed_actions(node, mousebind->actions);
}

static void
fill_mouse_context(xmlNode *node)
{
	char context_name[256];
	if (!lab_xml_get_string(node, "name", context_name, sizeof(context_name))) {
		return;
	}

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "mousebind")) {
			fill_mousebind(child, context_name);
		}
	}
}

static void
fill_touch(xmlNode *node)
{
	rc.touch_configs.push_back(touch_config_entry());
	auto touch_config = &rc.touch_configs.back();

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (!strcasecmp(key, "deviceName")) {
			touch_config->device_name = lab_str(content);
		} else if (!strcasecmp(key, "mapToOutput")) {
			touch_config->output_name = lab_str(content);
		} else if (!strcasecmp(key, "mouseEmulation")) {
			set_bool(content, &touch_config->force_mouse_emulation);
		} else {
			wlr_log(WLR_ERROR, "Unexpected data in touch parser: %s=\"%s\"",
				key, content);
		}
	}
}

static void
fill_tablet_button_map(xmlNode *node)
{
	uint32_t map_from;
	uint32_t map_to;
	char buf[256];

	if (lab_xml_get_string(node, "button", buf, sizeof(buf))) {
		map_from = tablet_button_from_str(buf);
	} else {
		wlr_log(WLR_ERROR, "Invalid 'button' argument for tablet button mapping");
		return;
	}

	if (lab_xml_get_string(node, "to", buf, sizeof(buf))) {
		map_to = mousebind_button_from_str(buf, NULL);
	} else {
		wlr_log(WLR_ERROR, "Invalid 'to' argument for tablet button mapping");
		return;
	}

	tablet_button_mapping_add(map_from, map_to);
}

static int
get_accel_profile(const char *s)
{
	if (!s) {
		return -1;
	}
	if (!strcasecmp(s, "flat")) {
		return LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
	}
	if (!strcasecmp(s, "adaptive")) {
		return LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
	}
	return -1;
}

static int
get_send_events_mode(const char *s)
{
	if (!s) {
		goto err;
	}

{ /* !goto */
	int ret = parse_bool(s, -1);
	if (ret >= 0) {
		return ret
			? LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
			: LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
	}

	if (!strcasecmp(s, "disabledOnExternalMouse")) {
		return LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
	}

} err:
	wlr_log(WLR_INFO, "Not a recognised send events mode");
	return -1;
}

static void
fill_libinput_category(xmlNode *node)
{
	/*
	 * Create a new profile (libinput-category) on `<libinput><device>`
	 * so that the 'default' profile can be created without even providing a
	 * category="" attribute (same as <device category="default">...)
	 */
	struct libinput_category *category = libinput_category_create();

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		if (string_null_or_empty(content)) {
			continue;
		}
		if (!strcmp(key, "category")) {
			/*
			 * First we try to get a type based on a number of
			 * pre-defined terms, for example: 'default', 'touch',
			 * 'touchpad' and 'non-touch'
			 */
			category->type = get_device_type(content);

			/*
			 * If we couldn't match against any of those terms, we
			 * use the provided value to define the device name
			 * that the settings should be applicable to.
			 */
			if (category->type == LAB_LIBINPUT_DEVICE_NONE) {
				category->name = lab_str(content);
			}
		} else if (!strcasecmp(key, "naturalScroll")) {
			set_bool_as_int(content, &category->natural_scroll);
		} else if (!strcasecmp(key, "leftHanded")) {
			set_bool_as_int(content, &category->left_handed);
		} else if (!strcasecmp(key, "pointerSpeed")) {
			set_float(content, &category->pointer_speed);
			if (category->pointer_speed < -1) {
				category->pointer_speed = -1;
			} else if (category->pointer_speed > 1) {
				category->pointer_speed = 1;
			}
		} else if (!strcasecmp(key, "tap")) {
			int ret = parse_bool(content, -1);
			if (ret < 0) {
				continue;
			}
			category->tap = ret
				? LIBINPUT_CONFIG_TAP_ENABLED
				: LIBINPUT_CONFIG_TAP_DISABLED;
		} else if (!strcasecmp(key, "tapButtonMap")) {
			if (!strcmp(content, "lrm")) {
				category->tap_button_map =
					LIBINPUT_CONFIG_TAP_MAP_LRM;
			} else if (!strcmp(content, "lmr")) {
				category->tap_button_map =
					LIBINPUT_CONFIG_TAP_MAP_LMR;
			} else {
				wlr_log(WLR_ERROR, "invalid tapButtonMap");
			}
		} else if (!strcasecmp(key, "tapAndDrag")) {
			int ret = parse_bool(content, -1);
			if (ret < 0) {
				continue;
			}
			category->tap_and_drag = ret
				? LIBINPUT_CONFIG_DRAG_ENABLED
				: LIBINPUT_CONFIG_DRAG_DISABLED;
		} else if (!strcasecmp(key, "dragLock")) {
			if (!strcasecmp(content, "timeout")) {
				/* "timeout" enables drag-lock with timeout */
				category->drag_lock = LIBINPUT_CONFIG_DRAG_LOCK_ENABLED;
				continue;
			}
			int ret = parse_bool(content, -1);
			if (ret < 0) {
				continue;
			}
			/* "yes" enables drag-lock, without timeout if libinput >= 1.27 */
			int enabled = LIBINPUT_CONFIG_DRAG_LOCK_ENABLED;
#if HAVE_LIBINPUT_CONFIG_DRAG_LOCK_ENABLED_STICKY
			enabled = LIBINPUT_CONFIG_DRAG_LOCK_ENABLED_STICKY;
#endif
			category->drag_lock = ret ?
				enabled : LIBINPUT_CONFIG_DRAG_LOCK_DISABLED;
	} else if (!strcasecmp(key, "threeFingerDrag")) {
#if HAVE_LIBINPUT_CONFIG_3FG_DRAG_ENABLED_3FG
		if (!strcmp(content, "3")) {
			category->three_finger_drag =
				LIBINPUT_CONFIG_3FG_DRAG_ENABLED_3FG;
		} else if (!strcmp(content, "4")) {
			category->three_finger_drag =
				LIBINPUT_CONFIG_3FG_DRAG_ENABLED_4FG;
		} else {
			int ret = parse_bool(content, -1);
			if (ret < 0) {
				continue;
			}
			category->three_finger_drag = ret
				? LIBINPUT_CONFIG_3FG_DRAG_ENABLED_3FG
				: LIBINPUT_CONFIG_3FG_DRAG_DISABLED;
		}
#else
		wlr_log(WLR_ERROR, "<threeFingerDrag> is only"
			" supported in libinput >= 1.28");
#endif
		} else if (!strcasecmp(key, "accelProfile")) {
			category->accel_profile =
				get_accel_profile(content);
		} else if (!strcasecmp(key, "middleEmulation")) {
			int ret = parse_bool(content, -1);
			if (ret < 0) {
				continue;
			}
			category->middle_emu = ret
				? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
				: LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED;
		} else if (!strcasecmp(key, "disableWhileTyping")) {
			int ret = parse_bool(content, -1);
			if (ret < 0) {
				continue;
			}
			category->dwt = ret
				? LIBINPUT_CONFIG_DWT_ENABLED
				: LIBINPUT_CONFIG_DWT_DISABLED;
		} else if (!strcasecmp(key, "clickMethod")) {
			if (!strcasecmp(content, "none")) {
				category->click_method =
					LIBINPUT_CONFIG_CLICK_METHOD_NONE;
			} else if (!strcasecmp(content, "clickfinger")) {
				category->click_method =
					LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
			} else if (!strcasecmp(content, "buttonAreas")) {
				category->click_method =
					LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
			} else {
				wlr_log(WLR_ERROR, "invalid clickMethod");
			}
		} else if (!strcasecmp(key, "scrollMethod")) {
			if (!strcasecmp(content, "none")) {
				category->scroll_method =
					LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
			} else if (!strcasecmp(content, "edge")) {
				category->scroll_method =
					LIBINPUT_CONFIG_SCROLL_EDGE;
			} else if (!strcasecmp(content, "twofinger")) {
				category->scroll_method =
					LIBINPUT_CONFIG_SCROLL_2FG;
			} else {
				wlr_log(WLR_ERROR, "invalid scrollMethod");
			}
		} else if (!strcasecmp(key, "sendEventsMode")) {
			category->send_events_mode =
				get_send_events_mode(content);
		} else if (!strcasecmp(key, "calibrationMatrix")) {
			errno = 0;
			category->have_calibration_matrix = true;
			float *mat = category->calibration_matrix;
			gchar **elements = g_strsplit(content, " ", -1);
			guint i = 0;
			for (; elements[i]; ++i) {
				char *end_str = NULL;
				mat[i] = strtof(elements[i], &end_str);
				if (errno == ERANGE || *end_str != '\0' || i == 6
						|| *elements[i] == '\0') {
					wlr_log(WLR_ERROR, "invalid calibration "
						"matrix element %s (index %d), "
						"expect six floats", elements[i], i);
					category->have_calibration_matrix = false;
					errno = 0;
					break;
				}
			}
			if (i != 6 && category->have_calibration_matrix) {
				wlr_log(WLR_ERROR, "wrong number of calibration "
					"matrix elements, expected 6, got %d", i);
				category->have_calibration_matrix = false;
			}
			g_strfreev(elements);
		} else if (!strcasecmp(key, "scrollFactor")) {
			set_double(content, &category->scroll_factor);
		}
	}
}

static void
set_font_attr(struct font *font, const char *nodename, const char *content)
{
	if (!strcmp(nodename, "name")) {
		font->name = lab_str(content);
	} else if (!strcmp(nodename, "size")) {
		font->size = atoi(content);
	} else if (!strcmp(nodename, "slant")) {
		if (!strcasecmp(content, "italic")) {
			font->slant = PANGO_STYLE_ITALIC;
		} else if (!strcasecmp(content, "oblique")) {
			font->slant = PANGO_STYLE_OBLIQUE;
		} else {
			font->slant = PANGO_STYLE_NORMAL;
		}
	} else if (!strcmp(nodename, "weight")) {
		if (!strcasecmp(content, "thin")) {
			font->weight = PANGO_WEIGHT_THIN;
		} else if (!strcasecmp(content, "ultralight")) {
			font->weight = PANGO_WEIGHT_ULTRALIGHT;
		} else if (!strcasecmp(content, "light")) {
			font->weight = PANGO_WEIGHT_LIGHT;
		} else if (!strcasecmp(content, "semilight")) {
			font->weight = PANGO_WEIGHT_SEMILIGHT;
		} else if (!strcasecmp(content, "book")) {
			font->weight = PANGO_WEIGHT_BOOK;
		} else if (!strcasecmp(content, "medium")) {
			font->weight = PANGO_WEIGHT_MEDIUM;
		} else if (!strcasecmp(content, "semibold")) {
			font->weight = PANGO_WEIGHT_SEMIBOLD;
		} else if (!strcasecmp(content, "bold")) {
			font->weight = PANGO_WEIGHT_BOLD;
		} else if (!strcasecmp(content, "ultrabold")) {
			font->weight = PANGO_WEIGHT_ULTRABOLD;
		} else if (!strcasecmp(content, "heavy")) {
			font->weight = PANGO_WEIGHT_HEAVY;
		} else if (!strcasecmp(content, "ultraheavy")) {
			font->weight = PANGO_WEIGHT_ULTRAHEAVY;
		} else {
			font->weight = PANGO_WEIGHT_NORMAL;
		}
	}
}

static enum font_place
enum_font_place(const char *place)
{
	if (!place || place[0] == '\0') {
		return FONT_PLACE_NONE;
	}
	if (!strcasecmp(place, "ActiveWindow")) {
		return FONT_PLACE_ACTIVEWINDOW;
	} else if (!strcasecmp(place, "InactiveWindow")) {
		return FONT_PLACE_INACTIVEWINDOW;
	} else if (!strcasecmp(place, "MenuHeader")) {
		return FONT_PLACE_MENUHEADER;
	} else if (!strcasecmp(place, "MenuItem")) {
		return FONT_PLACE_MENUITEM;
	} else if (!strcasecmp(place, "OnScreenDisplay")
			|| !strcasecmp(place, "OSD")) {
		return FONT_PLACE_OSD;
	}
	return FONT_PLACE_UNKNOWN;
}

static void
fill_font(xmlNode *node)
{
	enum font_place font_place = FONT_PLACE_NONE;
	char buf[256];
	if (lab_xml_get_string(node, "place", buf, sizeof(buf))) {
		font_place = enum_font_place(buf);
	}

	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		switch (font_place) {
		case FONT_PLACE_NONE:
			/*
			 * If <theme><font></font></theme> is used without a
			 * place="" attribute, we set all font variables
			 */
			set_font_attr(&rc.font_activewindow, key, content);
			set_font_attr(&rc.font_inactivewindow, key, content);
			set_font_attr(&rc.font_menuheader, key, content);
			set_font_attr(&rc.font_menuitem, key, content);
			set_font_attr(&rc.font_osd, key, content);
			break;
		case FONT_PLACE_ACTIVEWINDOW:
			set_font_attr(&rc.font_activewindow, key, content);
			break;
		case FONT_PLACE_INACTIVEWINDOW:
			set_font_attr(&rc.font_inactivewindow, key, content);
			break;
		case FONT_PLACE_MENUHEADER:
			set_font_attr(&rc.font_menuheader, key, content);
			break;
		case FONT_PLACE_MENUITEM:
			set_font_attr(&rc.font_menuitem, key, content);
			break;
		case FONT_PLACE_OSD:
			set_font_attr(&rc.font_osd, key, content);
			break;

			/* TODO: implement for all font places */

		default:
			break;
		}
	}
}

static void
set_adaptive_sync_mode(const char *str, enum adaptive_sync_mode *variable)
{
	if (!strcasecmp(str, "fullscreen")) {
		*variable = LAB_ADAPTIVE_SYNC_FULLSCREEN;
	} else {
		int ret = parse_bool(str, -1);
		if (ret == 1) {
			*variable = LAB_ADAPTIVE_SYNC_ENABLED;
		} else {
			*variable = LAB_ADAPTIVE_SYNC_DISABLED;
		}
	}
}

static void
set_tearing_mode(const char *str, enum tearing_mode *variable)
{
	if (!strcasecmp(str, "fullscreen")) {
		*variable = LAB_TEARING_FULLSCREEN;
	} else if (!strcasecmp(str, "fullscreenForced")) {
		*variable = LAB_TEARING_FULLSCREEN_FORCED;
	} else if (parse_bool(str, -1) == 1) {
		*variable = LAB_TEARING_ENABLED;
	} else {
		*variable = LAB_TEARING_DISABLED;
	}
}

/* Returns true if the node's children should also be traversed */
static bool
entry(xmlNode *node, char *nodename, char *content)
{
	string_truncate_at_pattern(nodename, ".openbox_config");
	string_truncate_at_pattern(nodename, ".labwc_config");

	if (getenv("LABWC_DEBUG_CONFIG_NODENAMES")) {
		printf("%s: %s\n", nodename, content);
	}

	/* handle nested nodes */
	if (!strcasecmp(nodename, "margin")) {
		fill_usable_area_override(node);
	} else if (!strcasecmp(nodename, "keybind.keyboard")) {
		fill_keybind(node);
	} else if (!strcasecmp(nodename, "context.mouse")) {
		fill_mouse_context(node);
	} else if (!strcasecmp(nodename, "touch")) {
		fill_touch(node);
	} else if (!strcasecmp(nodename, "device.libinput")) {
		fill_libinput_category(node);
	} else if (!strcasecmp(nodename, "regions")) {
		fill_regions(node);
	} else if (!strcasecmp(nodename, "fields.windowSwitcher")) {
		fill_window_switcher_fields(node);
	} else if (!strcasecmp(nodename, "windowRules")) {
		fill_window_rules(node);
	} else if (!strcasecmp(nodename, "font.theme")) {
		fill_font(node);
	} else if (!strcasecmp(nodename, "map.tablet")) {
		fill_tablet_button_map(node);

	/* handle nodes without content, e.g. <keyboard><default /> */
	} else if (!strcmp(nodename, "default.keyboard")) {
		load_default_key_bindings();
	} else if (!strcmp(nodename, "default.mouse")) {
		load_default_mouse_bindings();
	} else if (!strcasecmp(nodename, "prefix.desktops")) {
		rc.workspace_config.prefix = lab_str(content);

	} else if (!lab_xml_node_is_leaf(node)) {
		/* parse children of nested nodes other than above */
		return true;

	} else if (str_space_only(content)) {
		/* ignore empty leaf nodes other than above */

	/* handle non-empty leaf nodes */
	} else if (!strcmp(nodename, "decoration.core")) {
		if (!strcmp(content, "client")) {
			rc.xdg_shell_server_side_deco = false;
		} else {
			rc.xdg_shell_server_side_deco = true;
		}
	} else if (!strcmp(nodename, "gap.core")) {
		rc.gap = atoi(content);
	} else if (!strcasecmp(nodename, "adaptiveSync.core")) {
		set_adaptive_sync_mode(content, &rc.adaptive_sync);
	} else if (!strcasecmp(nodename, "allowTearing.core")) {
		set_tearing_mode(content, &rc.allow_tearing);
	} else if (!strcasecmp(nodename, "autoEnableOutputs.core")) {
		set_bool(content, &rc.auto_enable_outputs);
	} else if (!strcasecmp(nodename, "reuseOutputMode.core")) {
		set_bool(content, &rc.reuse_output_mode);
	} else if (!strcasecmp(nodename, "xwaylandPersistence.core")) {
		set_bool(content, &rc.xwayland_persistence);
	} else if (!strcasecmp(nodename, "primarySelection.core")) {
		set_bool(content, &rc.primary_selection);

	} else if (!strcasecmp(nodename, "promptCommand.core")) {
		rc.prompt_command = lab_str(content);

	} else if (!strcmp(nodename, "policy.placement")) {
		enum lab_placement_policy policy = view_placement_parse(content);
		if (policy != LAB_PLACE_INVALID) {
			rc.placement_policy = policy;
		}
	} else if (!strcasecmp(nodename, "x.cascadeOffset.placement")) {
		rc.placement_cascade_offset_x = atoi(content);
	} else if (!strcasecmp(nodename, "y.cascadeOffset.placement")) {
		rc.placement_cascade_offset_y = atoi(content);
	} else if (!strcmp(nodename, "name.theme")) {
		rc.theme_name = lab_str(content);
	} else if (!strcmp(nodename, "icon.theme")) {
		rc.icon_theme_name = lab_str(content);
	} else if (!strcasecmp(nodename, "fallbackAppIcon.theme")) {
		rc.fallback_app_icon_name = lab_str(content);
	} else if (!strcasecmp(nodename, "layout.titlebar.theme")) {
		fill_title_layout(content);
	} else if (!strcasecmp(nodename, "showTitle.titlebar.theme")) {
		rc.show_title = parse_bool(content, true);
	} else if (!strcmp(nodename, "cornerradius.theme")) {
		rc.corner_radius = atoi(content);
	} else if (!strcasecmp(nodename, "keepBorder.theme")) {
		set_bool(content, &rc.ssd_keep_border);
	} else if (!strcasecmp(nodename, "maximizedDecoration.theme")) {
		if (!strcasecmp(content, "titlebar")) {
			rc.hide_maximized_window_titlebar = false;
		} else if (!strcasecmp(content, "none")) {
			rc.hide_maximized_window_titlebar = true;
		}
	} else if (!strcasecmp(nodename, "dropShadows.theme")) {
		set_bool(content, &rc.shadows_enabled);
	} else if (!strcasecmp(nodename, "dropShadowsOnTiled.theme")) {
		set_bool(content, &rc.shadows_on_tiled);
	} else if (!strcasecmp(nodename, "followMouse.focus")) {
		set_bool(content, &rc.focus_follow_mouse);
	} else if (!strcasecmp(nodename, "followMouseRequiresMovement.focus")) {
		set_bool(content, &rc.focus_follow_mouse_requires_movement);
	} else if (!strcasecmp(nodename, "raiseOnFocus.focus")) {
		set_bool(content, &rc.raise_on_focus);
	} else if (!strcasecmp(nodename, "doubleClickTime.mouse")) {
		long doubleclick_time_parsed = strtol(content, NULL, 10);
		if (doubleclick_time_parsed > 0) {
			rc.doubleclick_time = doubleclick_time_parsed;
		} else {
			wlr_log(WLR_ERROR, "invalid doubleClickTime");
		}
	} else if (!strcasecmp(nodename, "scrollFactor.mouse")) {
		/* This is deprecated. Show an error message in post_processing() */
		set_double(content, &mouse_scroll_factor);

	} else if (!strcasecmp(nodename, "repeatRate.keyboard")) {
		rc.repeat_rate = atoi(content);
	} else if (!strcasecmp(nodename, "repeatDelay.keyboard")) {
		rc.repeat_delay = atoi(content);
	} else if (!strcasecmp(nodename, "numlock.keyboard")) {
		bool value;
		set_bool(content, &value);
		rc.kb_numlock_enable = value ? LAB_STATE_ENABLED
			: LAB_STATE_DISABLED;
	} else if (!strcasecmp(nodename, "layoutScope.keyboard")) {
		/*
		 * This can be changed to an enum later on
		 * if we decide to also support "application".
		 */
		rc.kb_layout_per_window = !strcasecmp(content, "window");
	} else if (!strcasecmp(nodename, "screenEdgeStrength.resistance")) {
		rc.screen_edge_strength = atoi(content);
	} else if (!strcasecmp(nodename, "windowEdgeStrength.resistance")) {
		rc.window_edge_strength = atoi(content);
	} else if (!strcasecmp(nodename, "unSnapThreshold.resistance")) {
		rc.unsnap_threshold = atoi(content);
	} else if (!strcasecmp(nodename, "unMaximizeThreshold.resistance")) {
		rc.unmaximize_threshold = atoi(content);
	} else if (!strcasecmp(nodename, "range.snapping")) {
		rc.snap_edge_range = atoi(content);
	} else if (!strcasecmp(nodename, "cornerRange.snapping")) {
		rc.snap_edge_corner_range = atoi(content);
	} else if (!strcasecmp(nodename, "enabled.overlay.snapping")) {
		set_bool(content, &rc.snap_overlay_enabled);
	} else if (!strcasecmp(nodename, "inner.delay.overlay.snapping")) {
		rc.snap_overlay_delay_inner = atoi(content);
	} else if (!strcasecmp(nodename, "outer.delay.overlay.snapping")) {
		rc.snap_overlay_delay_outer = atoi(content);
	} else if (!strcasecmp(nodename, "topMaximize.snapping")) {
		set_bool(content, &rc.snap_top_maximize);
	} else if (!strcasecmp(nodename, "notifyClient.snapping")) {
		if (!strcasecmp(content, "always")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_ALWAYS;
		} else if (!strcasecmp(content, "region")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_REGION;
		} else if (!strcasecmp(content, "edge")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_EDGE;
		} else if (!strcasecmp(content, "never")) {
			rc.snap_tiling_events_mode = LAB_TILING_EVENTS_NEVER;
		} else {
			wlr_log(WLR_ERROR, "ignoring invalid value for notifyClient");
		}

	/* <windowSwitcher show="" preview="" outlines="" /> */
	} else if (!strcasecmp(nodename, "show.windowSwitcher")) {
		set_bool(content, &rc.window_switcher.show);
	} else if (!strcasecmp(nodename, "style.windowSwitcher")) {
		if (!strcasecmp(content, "classic")) {
			rc.window_switcher.style = WINDOW_SWITCHER_CLASSIC;
		} else if (!strcasecmp(content, "thumbnail")) {
			rc.window_switcher.style = WINDOW_SWITCHER_THUMBNAIL;
		}
	} else if (!strcasecmp(nodename, "preview.windowSwitcher")) {
		set_bool(content, &rc.window_switcher.preview);
	} else if (!strcasecmp(nodename, "outlines.windowSwitcher")) {
		set_bool(content, &rc.window_switcher.outlines);
	} else if (!strcasecmp(nodename, "allWorkspaces.windowSwitcher")) {
		if (parse_bool(content, -1) == true) {
			rc.window_switcher.criteria =
				(lab_view_criteria)(rc.window_switcher.criteria
					& ~LAB_VIEW_CRITERIA_CURRENT_WORKSPACE);
		}

	/* Remove this long term - just a friendly warning for now */
	} else if (strstr(nodename, "windowswitcher.core")) {
		wlr_log(WLR_ERROR, "<windowSwitcher> should not be child of <core>");

	/* The following three are for backward compatibility only */
	} else if (!strcasecmp(nodename, "show.windowSwitcher.core")) {
		set_bool(content, &rc.window_switcher.show);
	} else if (!strcasecmp(nodename, "preview.windowSwitcher.core")) {
		set_bool(content, &rc.window_switcher.preview);
	} else if (!strcasecmp(nodename, "outlines.windowSwitcher.core")) {
		set_bool(content, &rc.window_switcher.outlines);

	/* The following three are for backward compatibility only */
	} else if (!strcasecmp(nodename, "cycleViewOSD.core")) {
		set_bool(content, &rc.window_switcher.show);
		wlr_log(WLR_ERROR, "<cycleViewOSD> is deprecated."
			" Use <windowSwitcher show=\"\" />");
	} else if (!strcasecmp(nodename, "cycleViewPreview.core")) {
		set_bool(content, &rc.window_switcher.preview);
		wlr_log(WLR_ERROR, "<cycleViewPreview> is deprecated."
			" Use <windowSwitcher preview=\"\" />");
	} else if (!strcasecmp(nodename, "cycleViewOutlines.core")) {
		set_bool(content, &rc.window_switcher.outlines);
		wlr_log(WLR_ERROR, "<cycleViewOutlines> is deprecated."
			" Use <windowSwitcher outlines=\"\" />");

	} else if (!strcasecmp(nodename, "name.names.desktops")) {
		rc.workspace_config.names.push_back(lab_str(content));
	} else if (!strcasecmp(nodename, "popupTime.desktops")) {
		rc.workspace_config.popuptime = atoi(content);
	} else if (!strcasecmp(nodename, "number.desktops")) {
		rc.workspace_config.min_nr_workspaces = MAX(1, atoi(content));
	} else if (!strcasecmp(nodename, "popupShow.resize")) {
		if (!strcasecmp(content, "Always")) {
			rc.resize_indicator = LAB_RESIZE_INDICATOR_ALWAYS;
		} else if (!strcasecmp(content, "Never")) {
			rc.resize_indicator = LAB_RESIZE_INDICATOR_NEVER;
		} else if (!strcasecmp(content, "Nonpixel")) {
			rc.resize_indicator = LAB_RESIZE_INDICATOR_NON_PIXEL;
		} else {
			wlr_log(WLR_ERROR, "Invalid value for <resize popupShow />");
		}
	} else if (!strcasecmp(nodename, "drawContents.resize")) {
		set_bool(content, &rc.resize_draw_contents);
	} else if (!strcasecmp(nodename, "cornerRange.resize")) {
		rc.resize_corner_range = atoi(content);
	} else if (!strcasecmp(nodename, "minimumArea.resize")) {
		rc.resize_minimum_area = MAX(0, atoi(content));
	} else if (!strcasecmp(nodename, "mouseEmulation.tablet")) {
		set_bool(content, &rc.tablet.force_mouse_emulation);
	} else if (!strcasecmp(nodename, "mapToOutput.tablet")) {
		rc.tablet.output_name = lab_str(content);
	} else if (!strcasecmp(nodename, "rotate.tablet")) {
		rc.tablet.rotation = tablet_parse_rotation(atoi(content));
	} else if (!strcasecmp(nodename, "left.area.tablet")) {
		rc.tablet.box.x = tablet_get_dbl_if_positive(content, "left");
	} else if (!strcasecmp(nodename, "top.area.tablet")) {
		rc.tablet.box.y = tablet_get_dbl_if_positive(content, "top");
	} else if (!strcasecmp(nodename, "width.area.tablet")) {
		rc.tablet.box.width = tablet_get_dbl_if_positive(content, "width");
	} else if (!strcasecmp(nodename, "height.area.tablet")) {
		rc.tablet.box.height = tablet_get_dbl_if_positive(content, "height");
	} else if (!strcasecmp(nodename, "motion.tabletTool")) {
		rc.tablet_tool.motion = tablet_parse_motion(content);
	} else if (!strcasecmp(nodename, "relativeMotionSensitivity.tabletTool")) {
		rc.tablet_tool.relative_motion_sensitivity =
			tablet_get_dbl_if_positive(content, "relativeMotionSensitivity");
	} else if (!strcasecmp(nodename, "ignoreButtonReleasePeriod.menu")) {
		rc.menu_ignore_button_release_period = atoi(content);
	} else if (!strcasecmp(nodename, "showIcons.menu")) {
		set_bool(content, &rc.menu_show_icons);
	} else if (!strcasecmp(nodename, "width.magnifier")) {
		rc.mag_width = atoi(content);
	} else if (!strcasecmp(nodename, "height.magnifier")) {
		rc.mag_height = atoi(content);
	} else if (!strcasecmp(nodename, "initScale.magnifier")) {
		set_float(content, &rc.mag_scale);
		rc.mag_scale = MAX(1.0, rc.mag_scale);
	} else if (!strcasecmp(nodename, "increment.magnifier")) {
		set_float(content, &rc.mag_increment);
		rc.mag_increment = MAX(0, rc.mag_increment);
	} else if (!strcasecmp(nodename, "useFilter.magnifier")) {
		set_bool(content, &rc.mag_filter);
	}

	return false;
}

static void
traverse(xmlNode *node)
{
	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(node, child, key, content) {
		(void)key;
		char buffer[256];
		char *name = nodename(child, buffer, sizeof(buffer));
		if (entry(child, name, content)) {
			traverse(child);
		}
	}
}

static void
rcxml_parse_xml(const std::string &buf)
{
	int options = 0;
	xmlDoc *d = xmlReadMemory(buf.data(), buf.size(), NULL, NULL, options);
	if (!d) {
		wlr_log(WLR_ERROR, "error parsing config file");
		return;
	}
	xmlNode *root = xmlDocGetRootElement(d);

	lab_xml_expand_dotted_attributes(root);
	traverse(root);

	xmlFreeDoc(d);
	xmlCleanupParser();
}

static void
init_font_defaults(struct font *font)
{
	font->size = 10;
	font->slant = PANGO_STYLE_NORMAL;
	font->weight = PANGO_WEIGHT_NORMAL;
}

static void
rcxml_init(void)
{
	rc.placement_policy = LAB_PLACE_CASCADE;
	rc.placement_cascade_offset_x = 0;
	rc.placement_cascade_offset_y = 0;

	rc.xdg_shell_server_side_deco = true;
	rc.hide_maximized_window_titlebar = false;
	rc.show_title = true;
	rc.title_layout_loaded = false;
	rc.ssd_keep_border = true;
	rc.corner_radius = 8;
	rc.shadows_enabled = false;
	rc.shadows_on_tiled = false;

	rc.gap = 0;
	rc.adaptive_sync = LAB_ADAPTIVE_SYNC_DISABLED;
	rc.allow_tearing = LAB_TEARING_DISABLED;
	rc.auto_enable_outputs = true;
	rc.reuse_output_mode = false;
	rc.xwayland_persistence = false;
	rc.primary_selection = true;

	init_font_defaults(&rc.font_activewindow);
	init_font_defaults(&rc.font_inactivewindow);
	init_font_defaults(&rc.font_menuheader);
	init_font_defaults(&rc.font_menuitem);
	init_font_defaults(&rc.font_osd);

	rc.focus_follow_mouse = false;
	rc.focus_follow_mouse_requires_movement = true;
	rc.raise_on_focus = false;

	rc.doubleclick_time = 500;

	rc.tablet.force_mouse_emulation = false;
	rc.tablet.rotation = LAB_ROTATE_NONE;
	rc.tablet.box = (struct wlr_fbox){0};
	tablet_load_default_button_mappings();
	rc.tablet_tool.motion = LAB_MOTION_ABSOLUTE;
	rc.tablet_tool.relative_motion_sensitivity = 1.0;

	rc.repeat_rate = 25;
	rc.repeat_delay = 600;
	rc.kb_numlock_enable = LAB_STATE_UNSPECIFIED;
	rc.kb_layout_per_window = false;
	rc.screen_edge_strength = 20;
	rc.window_edge_strength = 20;
	rc.unsnap_threshold = 20;
	rc.unmaximize_threshold = 150;

	rc.snap_edge_range = 10;
	rc.snap_edge_corner_range = 50;
	rc.snap_overlay_enabled = true;
	rc.snap_overlay_delay_inner = 500;
	rc.snap_overlay_delay_outer = 500;
	rc.snap_top_maximize = true;
	rc.snap_tiling_events_mode = LAB_TILING_EVENTS_ALWAYS;

	rc.window_switcher.show = true;
	rc.window_switcher.style = WINDOW_SWITCHER_CLASSIC;
	rc.window_switcher.preview = true;
	rc.window_switcher.outlines = true;
	rc.window_switcher.criteria =
		(lab_view_criteria)(LAB_VIEW_CRITERIA_CURRENT_WORKSPACE
			| LAB_VIEW_CRITERIA_ROOT_TOPLEVEL
			| LAB_VIEW_CRITERIA_NO_SKIP_WINDOW_SWITCHER);

	rc.resize_indicator = LAB_RESIZE_INDICATOR_NEVER;
	rc.resize_draw_contents = true;
	rc.resize_corner_range = -1;
	rc.resize_minimum_area = 8;

	rc.workspace_config.popuptime = INT_MIN;
	rc.workspace_config.min_nr_workspaces = 1;

	rc.menu_ignore_button_release_period = 250;
	rc.menu_show_icons = true;

	rc.mag_width = 400;
	rc.mag_height = 400;
	rc.mag_scale = 2.0;
	rc.mag_increment = 0.2;
	rc.mag_filter = true;
}

static void
load_default_key_bindings(void)
{
	for (int i = 0; key_combos[i].binding; i++) {
		struct key_combos *current = &key_combos[i];
		auto k = keybind_append_new(rc.keybinds, current->binding);
		assert(k);
		auto action = action::append_new(k->actions, current->action);
		assert(action);

		for (size_t j = 0; j < ARRAY_SIZE(current->attributes); j++) {
			if (!current->attributes[j].name
					|| !current->attributes[j].value) {
				break;
			}
			action->add_arg_from_xml_node(
				current->attributes[j].name,
				current->attributes[j].value);
		}
	}
}

static void
load_default_mouse_bindings(void)
{
	uint32_t count = 0;
	mousebind *m = nullptr;
	for (int i = 0; mouse_combos[i].context; i++) {
		struct mouse_combos *current = &mouse_combos[i];
		if (i == 0
				|| strcmp(current->context, mouse_combos[i - 1].context)
				|| strcmp(current->button, mouse_combos[i - 1].button)
				|| strcmp(current->event, mouse_combos[i - 1].event)) {
			/* Create new mousebind */
			m = mousebind_append_new(rc.mousebinds,
				current->context);
			assert(m);

			m->mouse_event = mousebind_event_from_str(current->event);
			if (m->mouse_event == MOUSE_ACTION_SCROLL) {
				m->direction = mousebind_direction_from_str(current->button,
					&m->modifiers);
			} else {
				m->button = mousebind_button_from_str(current->button,
					&m->modifiers);
			}
			count++;
		}

		auto action = action::append_new(m->actions, current->action);
		assert(action);

		for (size_t j = 0; j < ARRAY_SIZE(current->attributes); j++) {
			if (!current->attributes[j].name
					|| !current->attributes[j].value) {
				break;
			}
			action->add_arg_from_xml_node(
				current->attributes[j].name,
				current->attributes[j].value);
		}
	}
	wlr_log(WLR_DEBUG, "Loaded %u merged mousebinds", count);
}

static void
deduplicate_mouse_bindings(void)
{
	// move to temp list, removing duplicates
	std::vector<mousebind> tmp;
	auto end = rc.mousebinds.end();
	for (auto iter = rc.mousebinds.begin(); iter != end; ++iter) {
		auto dup = std::find_if(iter + 1, end, [&](mousebind &m) {
			return mousebind_the_same(*iter, m);
		});
		if (dup == end) {
			tmp.push_back(std::move(*iter));
		}
	}

	uint32_t replaced = rc.mousebinds.size() - tmp.size();

	// move back to original list, removing empty
	rc.mousebinds.clear();
	std::copy_if(std::move_iterator(tmp.begin()),
		std::move_iterator(tmp.end()),
		std::back_inserter(rc.mousebinds),
		[](const mousebind &m) { return !m.actions.empty(); });

	uint32_t cleared = tmp.size() - rc.mousebinds.size();

	if (replaced) {
		wlr_log(WLR_DEBUG, "Replaced %u mousebinds", replaced);
	}
	if (cleared) {
		wlr_log(WLR_DEBUG, "Cleared %u mousebinds", cleared);
	}
}

static void
deduplicate_key_bindings(void)
{
	// move to temp list, removing duplicates
	std::vector<keybind> tmp;
	auto end = rc.keybinds.end();
	for (auto iter = rc.keybinds.begin(); iter != end; ++iter) {
		auto dup = std::find_if(iter + 1, end, [&](keybind &k) {
			return keybind_the_same(*iter, k);
		});
		if (dup == end) {
			tmp.push_back(std::move(*iter));
		}
	}

	uint32_t replaced = rc.keybinds.size() - tmp.size();

	// move back to original list, removing empty
	rc.keybinds.clear();
	std::copy_if(std::move_iterator(tmp.begin()),
		std::move_iterator(tmp.end()), std::back_inserter(rc.keybinds),
		[](const keybind &k) { return !k.actions.empty(); });

	uint32_t cleared = tmp.size() - rc.keybinds.size();

	if (replaced) {
		wlr_log(WLR_DEBUG, "Replaced %u keybinds", replaced);
	}
	if (cleared) {
		wlr_log(WLR_DEBUG, "Cleared %u keybinds", cleared);
	}
}

static void
load_default_window_switcher_fields(void)
{
	static const struct {
		enum window_switcher_field_content content;
		int width;
	} fields[] = {
#if HAVE_LIBSFDO
		{ LAB_FIELD_ICON, 5 },
		{ LAB_FIELD_DESKTOP_ENTRY_NAME, 30 },
		{ LAB_FIELD_TITLE, 65 },
#else
		{ LAB_FIELD_DESKTOP_ENTRY_NAME, 30 },
		{ LAB_FIELD_TITLE, 70 },
#endif
	};

	for (size_t i = 0; i < ARRAY_SIZE(fields); i++) {
		rc.window_switcher.fields.push_back({
			.content = fields[i].content,
			.width = fields[i].width
		});
	}
}

static void
post_processing(void)
{
	if (rc.keybinds.empty()) {
		wlr_log(WLR_INFO, "load default key bindings");
		load_default_key_bindings();
	}

	if (rc.mousebinds.empty()) {
		wlr_log(WLR_INFO, "load default mouse bindings");
		load_default_mouse_bindings();
	}

	if (!rc.prompt_command) {
		rc.prompt_command =
			lab_str("labnag "
				"--message '%m' "
				"--button-dismiss '%n' "
				"--button-dismiss '%y' "
				"--background-color '%b' "
				"--text-color '%t' "
				"--button-border-color '%t' "
				"--border-bottom-color '%t' "
				"--button-background-color '%b' "
				"--button-text-color '%t' "
				"--border-bottom-size 1 "
				"--button-border-size 3 "
				"--timeout 0");
	}
	if (!rc.fallback_app_icon_name) {
		rc.fallback_app_icon_name = lab_str("labwc");
	}

	if (!rc.icon_theme_name && rc.theme_name) {
		rc.icon_theme_name = lab_str(rc.theme_name);
	}

	if (!rc.title_layout_loaded) {
#if HAVE_LIBSFDO
		fill_title_layout("icon:iconify,max,close");
#else
		/*
		 * 'icon' is replaced with 'menu' in fill_title_layout() when
		 * libsfdo is not linked, but we also replace it here not to
		 * show error message with default settings.
		 */
		fill_title_layout("menu:iconify,max,close");
#endif
	}

	/*
	 * Replace all earlier bindings by later ones
	 * and clear the ones with an empty action list.
	 *
	 * This is required so users are able to remove
	 * a default binding by using the "None" action.
	 */
	deduplicate_key_bindings();
	deduplicate_mouse_bindings();

	if (!rc.font_activewindow.name) {
		rc.font_activewindow.name = lab_str("sans");
	}
	if (!rc.font_inactivewindow.name) {
		rc.font_inactivewindow.name = lab_str("sans");
	}
	if (!rc.font_menuheader.name) {
		rc.font_menuheader.name = lab_str("sans");
	}
	if (!rc.font_menuitem.name) {
		rc.font_menuitem.name = lab_str("sans");
	}
	if (!rc.font_osd.name) {
		rc.font_osd.name = lab_str("sans");
	}
	if (!libinput_category_get_default()) {
		/* So we set default values of <tap> and <scrollFactor> */
		struct libinput_category *l = libinput_category_create();
		/* Prevents unused variable warning when compiled without asserts */
		(void)l;
		assert(l && libinput_category_get_default() == l);
	}
	if (mouse_scroll_factor >= 0) {
		wlr_log(WLR_ERROR, "<mouse><scrollFactor> is deprecated"
				" and overwrites <libinput><scrollFactor>."
				" Use only <libinput><scrollFactor>.");
		for (auto &l : rc.libinput_categories) {
			l.scroll_factor = mouse_scroll_factor;
		}
	}

	int nr_workspaces = rc.workspace_config.names.size();
	if (nr_workspaces < rc.workspace_config.min_nr_workspaces) {
		if (!rc.workspace_config.prefix) {
			rc.workspace_config.prefix = lab_str(_("Workspace"));
		}

		for (int i = nr_workspaces; i < rc.workspace_config.min_nr_workspaces; i++) {
			lab_str buf;
			if (rc.workspace_config.prefix) {
				buf += strdup_printf("%s ",
					rc.workspace_config.prefix.c());
			}
			buf += strdup_printf("%d", i + 1);
			rc.workspace_config.names.push_back(buf);
		}
	}
	if (rc.workspace_config.popuptime == INT_MIN) {
		rc.workspace_config.popuptime = 1000;
	}
	if (rc.window_switcher.fields.empty()) {
		wlr_log(WLR_INFO, "load default window switcher fields");
		load_default_window_switcher_fields();
	}
}

static bool
is_invalid_action(action &action)
{
	if (!action.is_valid()) {
		wlr_log(WLR_ERROR, "Removed invalid action");
		return true; // invalid
	}
	return false; // valid
}

static void
validate_actions(void)
{
	for (auto &keybind : rc.keybinds) {
		lab::remove_if(keybind.actions, is_invalid_action);
	}

	for (auto &mousebind : rc.mousebinds) {
		lab::remove_if(mousebind.actions, is_invalid_action);
	}

	for (auto &rule : rc.window_rules) {
		lab::remove_if(rule.actions, is_invalid_action);
	}
}

static bool
is_invalid_rule(window_rule &rule)
{
	if (!rule.identifier && !rule.title && rule.window_type < 0
			&& !rule.sandbox_engine && !rule.sandbox_app_id) {
		wlr_log(WLR_ERROR, "Deleting rule as it has no criteria");
		return true; // invalid
	}
	return false; // valid
}

static bool
is_invalid_region(region_cfg &region)
{
	struct wlr_box box = region.percentage;
	bool invalid = !region.name
		|| box.x < 0 || box.x > 100
		|| box.y < 0 || box.y > 100
		|| box.width <= 0 || box.width > 100
		|| box.height <= 0 || box.height > 100;
	if (invalid) {
		wlr_log(WLR_ERROR,
			"Removing invalid region '%s': %d%% x %d%% @ %d%%,%d%%",
			region.name.c(), box.width, box.height, box.x, box.y);
	}
	return invalid;
}

static void
validate(void)
{
	/* Regions */
	lab::remove_if(rc.regions, is_invalid_region);
	/* Window-rule criteria */
	lab::remove_if(rc.window_rules, is_invalid_rule);

	validate_actions();

	/* OSD fields */
	int field_width_sum = 0;
	lab::remove_if(rc.window_switcher.fields, [&](auto &field) {
		field_width_sum += field.width;
		if (!osd_field_is_valid(&field) || field_width_sum > 100) {
			wlr_log(WLR_ERROR,
				"Deleting invalid window switcher field");
			return true; // invalid
		}
		return false; // valid
	});
}

void
rcxml_read(const char *filename)
{
	rcxml_init();

	std::vector<lab_str> paths;

	if (filename) {
		/* Honour command line argument -c <filename> */
		paths.push_back(lab_str(filename));
	} else {
		paths = paths_config_create("rc.xml");
	}

	/* Reading file into buffer before parsing - better for unit tests */
	int num_paths = paths.size();
	bool should_merge_config = rc.merge_config;

	/*
	 * This is the equivalent of a wl_list_for_each() which optionally
	 * iterates in reverse depending on 'should_merge_config'
	 *
	 * If not merging, we iterate forwards and break after the first
	 * iteration.
	 *
	 * If merging, we iterate backwards (least important XDG Base Dir first)
	 * and keep going.
	 */
	for (int idx = 0; idx < num_paths; idx++) {
		auto &path = should_merge_config ?
			paths[(num_paths - 1) - idx] : paths[idx];
		std::ifstream ifs(path);
		if (!ifs.good()) {
			continue;
		}

		wlr_log(WLR_INFO, "read config file %s", path.c());

		std::ostringstream oss;
		oss << ifs.rdbuf();
		rcxml_parse_xml(oss.str());
		if (!should_merge_config) {
			break;
		}
	};
	post_processing();
	validate();
}

void
rcxml_finish(void)
{
	rc = rcxml();

	/* Reset state vars for starting fresh when Reload is triggered */
	mouse_scroll_factor = -1;
}
