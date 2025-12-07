// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "menu/menu.h"
#include <assert.h>
#include <libxml/parser.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <functional>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "action.h"
#include "common/buf.h"
#include "common/dir.h"
#include "common/font.h"
#include "common/lab-scene-rect.h"
#include "common/spawn.h"
#include "common/string-helpers.h"
#include "common/xml.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "scaled-buffer/scaled-font-buffer.h"
#include "scaled-buffer/scaled-icon-buffer.h"
#include "theme.h"
#include "translate.h"
#include "view.h"
#include "workspaces.h"

#define PIPEMENU_MAX_BUF_SIZE 1048576  /* 1 MiB */
#define PIPEMENU_TIMEOUT_IN_MS 4000    /* 4 seconds */

#define ICON_SIZE (g_theme.menu_item_height - 2 * g_theme.menu_items_padding_y)

static bool waiting_for_pipe_menu;
static weakptr<menuitem> selected_item;

struct menu_pipe_context {
	menu &pipemenu;
	struct wlr_box anchor_rect;
	struct buf buf;
	struct wl_event_source *event_read;
	struct wl_event_source *event_timeout;
	pid_t pid;
	int pipe_fd;

	~menu_pipe_context();
};

/* TODO: split this whole file into parser.c and actions.c*/

static bool
is_unique_id(const char *id)
{
	for (auto &menu : g_server.menus) {
		if (menu.id == id) {
			return false;
		}
	}
	return true;
}

static struct menu *
menu_create(struct menu *parent, const char *id, const char *label)
{
	if (!is_unique_id(id)) {
		wlr_log(WLR_ERROR, "menu id %s already exists", id);
	}

	auto menu = new struct menu();
	g_server.menus.append(menu);

	menu->id = lab_str(id);
	menu->label = lab_str(label ? label : id);
	menu->parent = weakptr(parent);
	menu->is_pipemenu_child = waiting_for_pipe_menu;
	return menu;
}

struct menu *
menu_get_by_id(const char *id)
{
	if (!id) {
		return NULL;
	}
	for (auto &menu : g_server.menus) {
		if (menu.id == id) {
			return &menu;
		}
	}
	return NULL;
}

static bool
is_invalid_action(action &action)
{
	bool is_show_menu = (action.type == ACTION_TYPE_SHOW_MENU);
	if (!action.is_valid() || is_show_menu) {
		if (is_show_menu) {
			wlr_log(WLR_ERROR, "'ShowMenu' action is"
				" not allowed in menu items");
		}
		wlr_log(WLR_ERROR, "Removed invalid menu action");
		return true; // invalid
	}
	return false; // valid
}

static void
validate(void)
{
	for (auto &menu : g_server.menus) {
		for (auto &item : menu.menuitems) {
			lab::remove_if(item.actions, is_invalid_action);
		}
	}
}

static struct menuitem *
item_create(struct menu *menu, const char *text, const char *icon_name, bool show_arrow)
{
	assert(menu);
	assert(text);

	auto menuitem = new (struct menuitem){.parent = *menu};
	menuitem->selectable = true;
	menuitem->type = LAB_MENU_ITEM;
	menuitem->text = lab_str(text);
	menuitem->arrow = show_arrow ? "›" : NULL;

#if HAVE_LIBSFDO
	if (rc.menu_show_icons && !string_null_or_empty(icon_name)) {
		menuitem->icon_name = lab_str(icon_name);
		menu->has_icons = true;
	}
#endif

	menuitem->native_width = font_width(&rc.font_menuitem, text);
	if (menuitem->arrow) {
		menuitem->native_width +=
			font_width(&rc.font_menuitem, menuitem->arrow)
				+ g_theme.menu_items_padding_x;
	}

	menu->menuitems.append(menuitem);
	return menuitem;
}

static struct wlr_scene_tree *
item_create_scene_for_state(struct menuitem *item, float *text_color,
	float *bg_color)
{
	auto &menu = item->parent;

	/* Tree to hold background and label buffers */
	struct wlr_scene_tree *tree = wlr_scene_tree_create(item->tree);

	int icon_width = 0;
	int icon_size = ICON_SIZE;
	if (menu.has_icons) {
		icon_width = g_theme.menu_items_padding_x + icon_size;
	}

	int bg_width = menu.size.width - 2 * g_theme.menu_border_width;
	int arrow_width = item->arrow
		? font_width(&rc.font_menuitem, item->arrow)
			+ g_theme.menu_items_padding_x
		: 0;
	int label_max_width = bg_width - 2 * g_theme.menu_items_padding_x
		- arrow_width - icon_width;

	if (label_max_width <= 0) {
		wlr_log(WLR_ERROR, "not enough space for menu contents");
		return tree;
	}

	/* Create background */
	wlr_scene_rect_create(tree, bg_width, g_theme.menu_item_height,
		bg_color);

	/* Create icon */
	bool show_app_icon = (menu.id == "client-list-combined-menu"
		&& item->client_list_view);
	if (item->icon_name || show_app_icon) {
		auto icon_buffer =
			new scaled_icon_buffer(tree, icon_size, icon_size);
		if (item->icon_name) {
			/* icon set via <menu icon="..."> */
			scaled_icon_buffer_set_icon_name(icon_buffer,
				item->icon_name.c());
		} else if (show_app_icon) {
			/* app icon in client-list-combined-menu */
			scaled_icon_buffer_set_view(icon_buffer,
				item->client_list_view);
		}
		wlr_scene_node_set_position(&icon_buffer->scene_buffer->node,
			g_theme.menu_items_padding_x,
			g_theme.menu_items_padding_y);
	}

	/* Create label */
	auto label_buffer = new scaled_font_buffer(tree);
	scaled_font_buffer_update(label_buffer, item->text.c(), label_max_width,
		&rc.font_menuitem, text_color, bg_color);
	/* Vertically center and left-align label */
	int x = g_theme.menu_items_padding_x + icon_width;
	int y = (g_theme.menu_item_height - label_buffer->height) / 2;
	wlr_scene_node_set_position(&label_buffer->scene_buffer->node, x, y);

	if (!item->arrow) {
		return tree;
	}

	/* Create arrow for submenu items */
	auto arrow_buffer = new scaled_font_buffer(tree);
	scaled_font_buffer_update(arrow_buffer, item->arrow, -1,
		&rc.font_menuitem, text_color, bg_color);
	/* Vertically center and right-align arrow */
	x += label_max_width + g_theme.menu_items_padding_x;
	y = (g_theme.menu_item_height - label_buffer->height) / 2;
	wlr_scene_node_set_position(&arrow_buffer->scene_buffer->node, x, y);

	return tree;
}

static void
item_create_scene(struct menuitem *menuitem, int *item_y)
{
	assert(menuitem);
	assert(menuitem->type == LAB_MENU_ITEM);
	auto &menu = menuitem->parent;

	/* Menu item root node */
	menuitem->tree = wlr_scene_tree_create(menu.scene_tree);
	node_descriptor_create(&menuitem->tree->node, LAB_NODE_MENUITEM,
		/*view*/ NULL, menuitem);

	/* Create scenes for unselected/selected states */
	menuitem->normal_tree = item_create_scene_for_state(menuitem,
		g_theme.menu_items_text_color, g_theme.menu_items_bg_color);
	menuitem->selected_tree = item_create_scene_for_state(menuitem,
		g_theme.menu_items_active_text_color,
		g_theme.menu_items_active_bg_color);
	/* Hide selected state */
	wlr_scene_node_set_enabled(&menuitem->selected_tree->node, false);

	/* Position the item in relation to its menu */
	wlr_scene_node_set_position(&menuitem->tree->node,
		g_theme.menu_border_width, *item_y);
	*item_y += g_theme.menu_item_height;
}

static struct menuitem *
separator_create(struct menu *menu, const char *label)
{
	assert(menu);

	auto menuitem = new (struct menuitem){.parent = *menu};
	menuitem->selectable = false;
	menuitem->type = string_null_or_empty(label) ? LAB_MENU_SEPARATOR_LINE
		: LAB_MENU_TITLE;
	if (menuitem->type == LAB_MENU_TITLE) {
		menuitem->text = lab_str(label);
		menuitem->native_width = font_width(&rc.font_menuheader, label);
	}

	menu->menuitems.append(menuitem);
	return menuitem;
}

static void
separator_create_scene(struct menuitem *menuitem, int *item_y)
{
	assert(menuitem);
	assert(menuitem->type == LAB_MENU_SEPARATOR_LINE);
	auto &menu = menuitem->parent;

	/* Menu item root node */
	menuitem->tree = wlr_scene_tree_create(menu.scene_tree);
	node_descriptor_create(&menuitem->tree->node, LAB_NODE_MENUITEM,
		/*view*/ NULL, menuitem);

	/* Tree to hold background and line buffer */
	menuitem->normal_tree = wlr_scene_tree_create(menuitem->tree);

	int bg_height = g_theme.menu_separator_line_thickness
		+ 2 * g_theme.menu_separator_padding_height;
	int bg_width = menu.size.width - 2 * g_theme.menu_border_width;
	int line_width = bg_width - 2 * g_theme.menu_separator_padding_width;

	if (line_width <= 0) {
		wlr_log(WLR_ERROR, "not enough space for menu separator");
		goto error;
	}

{ /* !goto */
	/* Item background nodes */
	wlr_scene_rect_create(menuitem->normal_tree, bg_width, bg_height,
		g_theme.menu_items_bg_color);

	/* Draw separator line */
	struct wlr_scene_rect *line_rect =
		wlr_scene_rect_create(menuitem->normal_tree, line_width,
			g_theme.menu_separator_line_thickness,
			g_theme.menu_separator_color);

	/* Vertically center-align separator line */
	wlr_scene_node_set_position(&line_rect->node,
		g_theme.menu_separator_padding_width,
		g_theme.menu_separator_padding_height);
} error:
	wlr_scene_node_set_position(&menuitem->tree->node,
		g_theme.menu_border_width, *item_y);
	*item_y += bg_height;
}

static void
title_create_scene(struct menuitem *menuitem, int *item_y)
{
	assert(menuitem);
	assert(menuitem->type == LAB_MENU_TITLE);
	auto &menu = menuitem->parent;
	float *bg_color = g_theme.menu_title_bg_color;
	float *text_color = g_theme.menu_title_text_color;

	/* Menu item root node */
	menuitem->tree = wlr_scene_tree_create(menu.scene_tree);
	node_descriptor_create(&menuitem->tree->node, LAB_NODE_MENUITEM,
		/*view*/ NULL, menuitem);

	/* Tree to hold background and text buffer */
	menuitem->normal_tree = wlr_scene_tree_create(menuitem->tree);

	int bg_width = menu.size.width - 2 * g_theme.menu_border_width;
	int text_width = bg_width - 2 * g_theme.menu_items_padding_x;

	if (text_width <= 0) {
		wlr_log(WLR_ERROR, "not enough space for menu title");
		goto error;
	}

{ /* !goto */
	/* Background */
	wlr_scene_rect_create(menuitem->normal_tree, bg_width,
		g_theme.menu_header_height, bg_color);

	/* Draw separator title */
	auto title_font_buffer = new scaled_font_buffer(menuitem->normal_tree);
	scaled_font_buffer_update(title_font_buffer, menuitem->text.c(),
		text_width, &rc.font_menuheader, text_color, bg_color);

	int title_x = 0;
	switch (g_theme.menu_title_text_justify) {
	case LAB_JUSTIFY_CENTER:
		title_x = (bg_width - menuitem->native_width) / 2;
		title_x = MAX(title_x, 0);
		break;
	case LAB_JUSTIFY_LEFT:
		title_x = g_theme.menu_items_padding_x;
		break;
	case LAB_JUSTIFY_RIGHT:
		title_x = bg_width - menuitem->native_width
			- g_theme.menu_items_padding_x;
		break;
	}
	int title_y = (g_theme.menu_header_height - title_font_buffer->height) / 2;
	wlr_scene_node_set_position(&title_font_buffer->scene_buffer->node,
		title_x, title_y);
} error:
	wlr_scene_node_set_position(&menuitem->tree->node,
		g_theme.menu_border_width, *item_y);
	*item_y += g_theme.menu_header_height;
}

static void
reset_menu(struct menu *menu)
{
	menu->menuitems.clear();
	if (menu->scene_tree) {
		wlr_scene_node_destroy(&menu->scene_tree->node);
		menu->scene_tree = NULL;
	}
	/* TODO: also reset other fields? */
}

static void
menu_create_scene(struct menu *menu)
{
	assert(!menu->scene_tree);

	menu->scene_tree = wlr_scene_tree_create(g_server.menu_tree);
	wlr_scene_node_set_enabled(&menu->scene_tree->node, false);

	/* Menu width is the maximum item width, capped by menu.width.{min,max} */
	menu->size.width = 0;
	for (auto &item : menu->menuitems) {
		int width = item.native_width + 2 * g_theme.menu_items_padding_x
			+ 2 * g_theme.menu_border_width;
		menu->size.width = MAX(menu->size.width, width);
	}

	if (menu->has_icons) {
		menu->size.width += g_theme.menu_items_padding_x + ICON_SIZE;
	}
	menu->size.width = MAX(menu->size.width, g_theme.menu_min_width);
	menu->size.width = MIN(menu->size.width, g_theme.menu_max_width);

	/* Update all items for the new size */
	int item_y = g_theme.menu_border_width;
	for (auto &item : menu->menuitems) {
		assert(!item.tree);
		switch (item.type) {
		case LAB_MENU_ITEM:
			item_create_scene(&item, &item_y);
			break;
		case LAB_MENU_SEPARATOR_LINE:
			separator_create_scene(&item, &item_y);
			break;
		case LAB_MENU_TITLE:
			title_create_scene(&item, &item_y);
			break;
		}
	}
	menu->size.height = item_y + g_theme.menu_border_width;

	float *border_color = g_theme.menu_border_color;
	struct lab_scene_rect_options opts = {
		.border_colors = &border_color,
		.nr_borders = 1,
		.border_width = g_theme.menu_border_width,
		.width = menu->size.width,
		.height = menu->size.height,
	};
	struct lab_scene_rect *bg_rect =
		lab_scene_rect_create(menu->scene_tree, &opts);
	wlr_scene_node_lower_to_bottom(&bg_rect->tree->node);
}

/*
 * Handle the following:
 * <item label="">
 *   <action name="">
 *     <command></command>
 *   </action>
 * </item>
 */
static void
fill_item(struct menu *menu, xmlNode *node)
{
	char *label = (char *)xmlGetProp(node, (xmlChar *)"label");
	char *icon_name = (char *)xmlGetProp(node, (xmlChar *)"icon");
	if (!label) {
		wlr_log(WLR_ERROR, "missing label in <item>");
		goto out;
	}

{ /* !goto */
	struct menuitem *item = item_create(menu, label, icon_name, false);
	lab_xml_expand_dotted_attributes(node);
	append_parsed_actions(node, item->actions);

} out:
	xmlFree(label);
	xmlFree(icon_name);
}

menuitem::~menuitem()
{
	if (tree) {
		wlr_scene_node_destroy(&tree->node);
	}
}

static bool parse_buf(struct menu *menu, struct buf *buf);
static int handle_pipemenu_readable(int fd, uint32_t mask, void *_ctx);
static int handle_pipemenu_timeout(void *_ctx);
static void fill_menu_children(struct menu *parent, xmlNode *n);

/*
 * <menu> elements have three different roles:
 *  * Definition of (sub)menu - has ID, LABEL and CONTENT
 *  * Menuitem of pipemenu type - has ID, LABEL and EXECUTE
 *  * Menuitem of submenu type - has ID only
 */
static void
fill_menu(struct menu *parent, xmlNode *n)
{
	char *label = (char *)xmlGetProp(n, (const xmlChar *)"label");
	char *icon_name = (char *)xmlGetProp(n, (const xmlChar *)"icon");
	char *execute = (char *)xmlGetProp(n, (const xmlChar *)"execute");
	char *id = (char *)xmlGetProp(n, (const xmlChar *)"id");

	if (!id) {
		wlr_log(WLR_ERROR, "<menu> without id is not allowed");
		goto error;
	}

	if (execute && label) {
		wlr_log(WLR_DEBUG, "pipemenu '%s:%s:%s'", id, label, execute);

		struct menu *pipemenu = menu_create(parent, id, label);
		pipemenu->execute = lab_str(execute);
		if (!parent) {
			/*
			 * A pipemenu may not have its parent like:
			 *
			 * <?xml version="1.0" encoding="UTF-8"?>
			 * <openbox_menu>
			 *   <menu id="root-menu" label="foo" execute="bar"/>
			 * </openbox_menu>
			 */
		} else {
			struct menuitem *item = item_create(parent, label,
				icon_name, /* arrow */ true);
			item->submenu.reset(pipemenu);
		}
	} else if ((label && parent) || !parent) {
		/*
		 * (label && parent) refers to <menu id="" label="">
		 * which is an nested (inline) menu definition.
		 *
		 * (!parent) catches:
		 *     <openbox_menu>
		 *       <menu id=""></menu>
		 *     </openbox_menu>
		 * or
		 *     <openbox_menu>
		 *       <menu id="" label=""></menu>
		 *     </openbox_menu>
		 *
		 * which is the highest level a menu can be defined at.
		 *
		 * Openbox spec requires a label="" defined here, but it is
		 * actually pointless so we handle it with or without the label
		 * attribute to make it easier for users to define "root-menu"
		 * and "client-menu".
		 */
		struct menu *menu = menu_create(parent, id, label);
		if (icon_name) {
			menu->icon_name = lab_str(icon_name);
		}
		if (label && parent) {
			/*
			 * In a nested (inline) menu definition we need to
			 * create an item pointing to the new submenu
			 */
			struct menuitem *item = item_create(parent, label,
				icon_name, true);
			item->submenu.reset(menu);
		}
		fill_menu_children(menu, n);
	} else {
		/*
		 * <menu id=""> (when inside another <menu> element) creates an
		 * entry which points to a menu defined elsewhere.
		 *
		 * This is only supported in static menus. Pipemenus need to use
		 * nested (inline) menu definitions, otherwise we could have a
		 * pipemenu opening the "root-menu" or similar.
		 */

		if (waiting_for_pipe_menu) {
			wlr_log(WLR_ERROR,
				"cannot link to static menu from pipemenu");
			goto error;
		}

		struct menu *menu = menu_get_by_id(id);
		if (!menu) {
			wlr_log(WLR_ERROR, "no menu with id '%s'", id);
			goto error;
		}

		struct menu *iter = parent;
		while (iter) {
			if (iter == menu) {
				wlr_log(WLR_ERROR, "menus with the same id '%s' "
					"cannot be nested", id);
				goto error;
			}
			iter = iter->parent.get();
		}

		struct menuitem *item = item_create(parent, menu->label.c(),
			icon_name ? icon_name : menu->icon_name.c(), true);
		item->submenu.reset(menu);
	}
error:
	xmlFree(label);
	xmlFree(icon_name);
	xmlFree(execute);
	xmlFree(id);
}

/* This can be one of <separator> and <separator label=""> */
static void
fill_separator(struct menu *menu, xmlNode *n)
{
	char *label = (char *)xmlGetProp(n, (const xmlChar *)"label");
	separator_create(menu, label);
	xmlFree(label);
}

/* parent==NULL when processing toplevel menus in menu.xml */
static void
fill_menu_children(struct menu *parent, xmlNode *n)
{
	xmlNode *child;
	char *key, *content;
	LAB_XML_FOR_EACH(n, child, key, content) {
		if (!strcasecmp(key, "menu")) {
			fill_menu(parent, child);
		} else if (!strcasecmp(key, "separator")) {
			if (!parent) {
				wlr_log(WLR_ERROR,
					"ignoring <separator> without parent <menu>");
				continue;
			}
			fill_separator(parent, child);
		} else if (!strcasecmp(key, "item")) {
			if (!parent) {
				wlr_log(WLR_ERROR,
					"ignoring <item> without parent <menu>");
				continue;
			}
			fill_item(parent, child);
		}
	}
}

static bool
parse_buf(struct menu *parent, struct buf *buf)
{
	int options = 0;
	xmlDoc *d = xmlReadMemory(buf->data, buf->len, NULL, NULL, options);
	if (!d) {
		wlr_log(WLR_ERROR, "xmlParseMemory()");
		return false;
	}

	xmlNode *root = xmlDocGetRootElement(d);
	fill_menu_children(parent, root);

	xmlFreeDoc(d);
	xmlCleanupParser();
	return true;
}

static void
parse_xml(const char *filename)
{
	auto paths = paths_config_create(filename);
	int num_paths = paths.size();

	bool should_merge_config = rc.merge_config;

	for (int idx = 0; idx < num_paths; idx++) {
		auto &path = should_merge_config ?
			paths[(num_paths - 1) - idx] : paths[idx];
		struct buf buf = buf_from_file(path.c());
		if (!buf.len) {
			continue;
		}
		wlr_log(WLR_INFO, "read menu file %s", path.c());
		parse_buf(/*parent*/ NULL, &buf);
		buf_reset(&buf);
		if (!should_merge_config) {
			break;
		}
	}
}

/*
 * Returns the box of a menuitem next to which its submenu is opened.
 * This box can be shrunk or expanded by menu overlaps and borders.
 */
static struct wlr_box
get_item_anchor_rect(struct menuitem *item)
{
	auto &menu = item->parent;
	int menu_x = menu.scene_tree->node.x;
	int menu_y = menu.scene_tree->node.y;
	int overlap_x = g_theme.menu_overlap_x + g_theme.menu_border_width;
	int overlap_y = g_theme.menu_overlap_y - g_theme.menu_border_width;
	return (struct wlr_box){
		.x = menu_x + overlap_x,
		.y = menu_y + item->tree->node.y + overlap_y,
		.width = menu.size.width - 2 * overlap_x,
		.height = g_theme.menu_item_height - 2 * overlap_y,
	};
}

static void
menu_reposition(struct menu *menu, struct wlr_box anchor_rect)
{
	/* Get output usable area to place the menu within */
	struct output *output = output_nearest_to(anchor_rect.x, anchor_rect.y);
	if (!output) {
		wlr_log(WLR_ERROR, "no output found around (%d,%d)",
			anchor_rect.x, anchor_rect.y);
		return;
	}
	struct wlr_box usable = output_usable_area_in_layout_coords(output);

	/* Policy for menu placement */
	struct wlr_xdg_positioner_rules rules = {};
	rules.size.width = menu->size.width;
	rules.size.height = menu->size.height;
	/* A rectangle next to which the menu is opened */
	rules.anchor_rect = anchor_rect;
	/*
	 * Place menu at left or right side of anchor_rect, with their
	 * top edges aligned. The alignment is inherited from parent.
	 */
	if (CHECK_PTR(menu->parent, parent) && parent->align_left) {
		rules.anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT;
		rules.gravity = XDG_POSITIONER_GRAVITY_BOTTOM_LEFT;
	} else {
		rules.anchor = XDG_POSITIONER_ANCHOR_TOP_RIGHT;
		rules.gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT;
	}
	/* Flip or slide the menu when it overflows from the output */
	rules.constraint_adjustment = (xdg_positioner_constraint_adjustment)
		(XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X
			| XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X
			| XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);
	if (!menu->parent) {
		/* Allow vertically flipping the root menu */
		rules.constraint_adjustment = (xdg_positioner_constraint_adjustment)
			(rules.constraint_adjustment
				| XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
	}

	struct wlr_box box;
	wlr_xdg_positioner_rules_get_geometry(&rules, &box);
	wlr_xdg_positioner_rules_unconstrain_box(&rules, &usable, &box);
	wlr_scene_node_set_position(&menu->scene_tree->node, box.x, box.y);

	menu->align_left = (box.x < anchor_rect.x);
}

static void
menu_hide_submenu(const char *id)
{
	struct menu *hide_menu;
	hide_menu = menu_get_by_id(id);
	if (!hide_menu) {
		return;
	}
	for (auto &menu : g_server.menus) {
		for (auto item = menu.menuitems.begin(); item; ++item) {
			if (item->submenu == hide_menu) {
				item.remove();
			}
		}
	}
}

static struct action *
item_add_action(struct menuitem *item, const char *action_name)
{
	return action::append_new(item->actions, action_name);
}

/*
 * This is client-send-to-menu
 * an internal menu similar to root-menu and client-menu
 *
 * This will look at workspaces and produce a menu
 * with the workspace names that can be used with
 * SendToDesktop, left/right options are included.
 */
static void
update_client_send_to_menu(void)
{
	struct menu *menu = menu_get_by_id("client-send-to-menu");
	assert(menu);

	reset_menu(menu);

	/*
	 * <action name="SendToDesktop"><follow> is true by default so
	 * GoToDesktop will be called as part of the action.
	 */
	struct buf buf = BUF_INIT;
	for (auto &workspace : g_server.workspaces.all) {
		if (&workspace == g_server.workspaces.current) {
			buf_add_fmt(&buf, ">%s<", workspace.name.c());
		} else {
			buf_add(&buf, workspace.name.c());
		}
		struct menuitem *item = item_create(menu, buf.data,
			NULL, /*show arrow*/ false);

		struct action *action = item_add_action(item, "SendToDesktop");
		action->add_str("to", workspace.name.c());

		buf_clear(&buf);
	}
	buf_reset(&buf);

	separator_create(menu, "");
	struct menuitem *item = item_create(menu,
		_("Always on Visible Workspace"), NULL, false);
	item_add_action(item, "ToggleOmnipresent");

	menu_create_scene(menu);
}

/*
 * This is client-list-combined-menu an internal menu similar to root-menu and
 * client-menu.
 *
 * This will look at workspaces and produce a menu with the workspace name as a
 * separator label and the titles of the view, if any, below each workspace
 * name. Active view is indicated by "*" preceding title.
 */
static void
update_client_list_combined_menu(void)
{
	struct menu *menu = menu_get_by_id("client-list-combined-menu");
	assert(menu);

	reset_menu(menu);

	struct menuitem *item;
	struct buf buffer = BUF_INIT;

	for (auto &workspace : g_server.workspaces.all) {
		buf_add_fmt(&buffer,
			&workspace == g_server.workspaces.current ? ">%s<" : "%s",
			workspace.name.c());
		separator_create(menu, buffer.data);
		buf_clear(&buffer);

		for (auto &view : g_views) {
			if (view.workspace.get() == &workspace) {
				if (!view.foreign_toplevel || !view.title) {
					continue;
				}

				if (&view == g_server.active_view) {
					buf_add(&buffer, "*");
				}
				if (view.minimized) {
					buf_add_fmt(&buffer, "(%s)", view.title.c());
				} else {
					buf_add(&buffer, view.title.c());
				}
				item = item_create(menu, buffer.data, NULL,
					/*show arrow*/ false);
				item->client_list_view = &view;
				item_add_action(item, "Focus");
				item_add_action(item, "Raise");
				buf_clear(&buffer);
				menu->has_icons = true;
			}
		}
		item = item_create(menu, _("Go there..."), NULL,
			/*show arrow*/ false);
		struct action *action = item_add_action(item, "GoToDesktop");
		action->add_str("to", workspace.name.c());
	}
	buf_reset(&buffer);
	menu_create_scene(menu);
}

static void
init_rootmenu(void)
{
	struct menu *menu = menu_get_by_id("root-menu");
	struct menuitem *item;

	/* Default menu if no menu.xml found */
	if (!menu) {
		menu = menu_create(NULL, "root-menu", "");

		item = item_create(menu, _("Terminal"), NULL, false);
		struct action *action = item_add_action(item, "Execute");
		action->add_str("command", "lab-sensible-terminal");

		separator_create(menu, NULL);

		item = item_create(menu, _("Reconfigure"), NULL, false);
		item_add_action(item, "Reconfigure");
		item = item_create(menu, _("Exit"), NULL, false);
		item_add_action(item, "Exit");
	}
}

static void
init_windowmenu(void)
{
	struct menu *menu = menu_get_by_id("client-menu");
	struct menuitem *item;

	/* Default menu if no menu.xml found */
	if (!menu) {
		menu = menu_create(NULL, "client-menu", "");
		item = item_create(menu, _("Minimize"), NULL, false);
		item_add_action(item, "Iconify");
		item = item_create(menu, _("Maximize"), NULL, false);
		item_add_action(item, "ToggleMaximize");
		item = item_create(menu, _("Fullscreen"), NULL, false);
		item_add_action(item, "ToggleFullscreen");
		item = item_create(menu, _("Roll Up/Down"), NULL, false);
		item_add_action(item, "ToggleShade");
		item = item_create(menu, _("Decorations"), NULL, false);
		item_add_action(item, "ToggleDecorations");
		item = item_create(menu, _("Always on Top"), NULL, false);
		item_add_action(item, "ToggleAlwaysOnTop");

		/* Workspace sub-menu */
		item = item_create(menu, _("Workspace"), NULL, true);
		item->submenu.reset(menu_get_by_id("client-send-to-menu"));

		item = item_create(menu, _("Close"), NULL, false);
		item_add_action(item, "Close");
	}

	if (rc.workspace_config.names.size() == 1) {
		menu_hide_submenu("workspaces");
	}
}

void
menu_init(void)
{
	/* Just create placeholder. Contents will be created when launched */
	menu_create(NULL, "client-list-combined-menu", _("Windows"));
	menu_create(NULL, "client-send-to-menu", _("Workspace"));

	parse_xml("menu.xml");
	init_rootmenu();
	init_windowmenu();
	validate();
}

menu::~menu()
{
	if (g_server.menu_current == this) {
		menu_close_root();
	}

	menuitems.clear();
	pipe_ctx.reset();

	/*
	 * Destroying the root node will destroy everything,
	 * including node descriptors and scaled_font_buffers.
	 */
	if (scene_tree) {
		wlr_scene_node_destroy(&scene_tree->node);
	}
}

void
menu_finish(void)
{
	g_server.menus.clear();
}

void
menu_on_view_destroy(struct view *view)
{
	/* If the view being destroy has an open window menu, then close it */
	if (CHECK_PTR(g_server.menu_current, current)
			&& current->triggered_by_view == view) {
		menu_close_root();
	}

	/*
	 * TODO: Instead of just setting client_list_view to NULL and deleting
	 * the actions (as below), consider destroying the item and somehow
	 * updating the menu and its selection state.
	 */

	/* Also nullify the destroyed view in client-list-combined-menu */
	struct menu *menu = menu_get_by_id("client-list-combined-menu");
	if (menu) {
		for (auto &item : menu->menuitems) {
			if (item.client_list_view == view) {
				item.client_list_view = NULL;
				item.actions.clear();
			}
		}
	}
}

/* Sets selection (or clears selection if passing NULL) */
static void
menu_set_selection(struct menu *menu, struct menuitem *item)
{
	/* Clear old selection */
	if (CHECK_PTR(menu->selection.item, old)) {
		wlr_scene_node_set_enabled(&old->normal_tree->node, true);
		wlr_scene_node_set_enabled(&old->selected_tree->node, false);
	}
	/* Set new selection */
	if (item) {
		wlr_scene_node_set_enabled(&item->normal_tree->node, false);
		wlr_scene_node_set_enabled(&item->selected_tree->node, true);
	}
	menu->selection.item.reset(item);
}

/*
 * We only destroy pipemenus when closing the entire menu-tree so that pipemenu
 * are cached (for as long as the menu is open). This drastically improves the
 * felt performance when interacting with multiple pipe menus where a single
 * item may be selected multiple times.
 */
static void
reset_pipemenus(void)
{
	wlr_log(WLR_DEBUG, "number of menus before close=%d",
		g_server.menus.size());

	for (auto menu = g_server.menus.begin(); menu; ++menu) {
		if (menu->is_pipemenu_child) {
			/* Destroy submenus of pipemenus */
			menu.remove();
		} else if (menu->execute) {
			/*
			 * Destroy items and scene-nodes of pipemenus so that
			 * they are generated again when being opened
			 */
			reset_menu(menu.get());
		}
	}

	wlr_log(WLR_DEBUG, "number of menus after  close=%d",
		g_server.menus.size());
}

static void
_close(struct menu *menu)
{
	if (menu->scene_tree) {
		wlr_scene_node_set_enabled(&menu->scene_tree->node, false);
	}
	menu_set_selection(menu, NULL);
	if (menu->selection.menu) {
		_close(menu->selection.menu.get());
		menu->selection.menu.reset();
	}
	menu->pipe_ctx.reset();
}

static void
menu_close(struct menu *menu)
{
	if (!menu) {
		wlr_log(WLR_ERROR, "Trying to close non exiting menu");
		return;
	}
	_close(menu);
}

static void
open_menu(struct menu *menu, struct wlr_box anchor_rect)
{
	if (menu->id == "client-list-combined-menu") {
		update_client_list_combined_menu();
	} else if (menu->id == "client-send-to-menu") {
		update_client_send_to_menu();
	}

	if (!menu->scene_tree) {
		menu_create_scene(menu);
		assert(menu->scene_tree);
	}
	menu_reposition(menu, anchor_rect);
	wlr_scene_node_set_enabled(&menu->scene_tree->node, true);
}

static void open_pipemenu_async(struct menu *pipemenu, struct wlr_box anchor_rect);

void
menu_open_root(struct menu *menu, int x, int y)
{
	assert(menu);

	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	assert(!g_server.menu_current);

	struct wlr_box anchor_rect = {.x = x, .y = y};
	if (menu->execute) {
		open_pipemenu_async(menu, anchor_rect);
	} else {
		open_menu(menu, anchor_rect);
	}

	g_server.menu_current.reset(menu);
	selected_item.reset();
	seat_focus_override_begin(LAB_INPUT_STATE_MENU, LAB_CURSOR_DEFAULT);
}

static void
create_pipe_menu(struct menu_pipe_context *ctx)
{
	if (!parse_buf(&ctx->pipemenu, &ctx->buf)) {
		return;
	}
	/* TODO: apply validate() only for generated pipemenus */
	validate();

	/* Finally open the new submenu tree */
	open_menu(&ctx->pipemenu, ctx->anchor_rect);
}

menu_pipe_context::~menu_pipe_context()
{
	wl_event_source_remove(event_read);
	wl_event_source_remove(event_timeout);
	spawn_piped_close(pid, pipe_fd);
	buf_reset(&buf);
	waiting_for_pipe_menu = false;
}

static int
handle_pipemenu_timeout(void *_ctx)
{
	auto ctx = (menu_pipe_context *)_ctx;
	wlr_log(WLR_ERROR, "[pipemenu %ld] timeout reached, killing %s",
		(long)ctx->pid, ctx->pipemenu.execute.c());
	kill(ctx->pid, SIGTERM);
	ctx->pipemenu.pipe_ctx.reset(); // deletes ctx
	return 0;
}

static int
handle_pipemenu_readable(int fd, uint32_t mask, void *_ctx)
{
	auto ctx = (menu_pipe_context *)_ctx;
	/* two 4k pages + 1 NULL byte */
	char data[8193];
	ssize_t size;

	do {
		/* leave space for terminating NULL byte */
		size = read(fd, data, sizeof(data) - 1);
	} while (size == -1 && errno == EINTR);

	if (size == -1) {
		wlr_log_errno(WLR_ERROR,
			"[pipemenu %ld] failed to read data (%s)",
			(long)ctx->pid, ctx->pipemenu.execute.c());
		goto clean_up;
	}

	/* Limit pipemenu buffer to 1 MiB for safety */
	if (ctx->buf.len + size > PIPEMENU_MAX_BUF_SIZE) {
		wlr_log(WLR_ERROR,
			"[pipemenu %ld] too big (> %d bytes); killing %s",
			(long)ctx->pid, PIPEMENU_MAX_BUF_SIZE,
			ctx->pipemenu.execute.c());
		kill(ctx->pid, SIGTERM);
		goto clean_up;
	}

	wlr_log(WLR_DEBUG, "[pipemenu %ld] read %ld bytes of data", (long)ctx->pid, size);
	if (size) {
		data[size] = '\0';
		buf_add(&ctx->buf, data);
		return 0;
	}

	/* Guard against badly formed data such as binary input */
	if (!str_starts_with(ctx->buf.data, '<', " \t\r\n")) {
		wlr_log(WLR_ERROR, "expect xml data to start with '<'; abort pipemenu");
		goto clean_up;
	}

	create_pipe_menu(ctx);

clean_up:
	ctx->pipemenu.pipe_ctx.reset(); // deletes ctx
	return 0;
}

static void
open_pipemenu_async(struct menu *pipemenu, struct wlr_box anchor_rect)
{
	assert(!pipemenu->pipe_ctx);
	assert(!pipemenu->scene_tree);

	int pipe_fd = 0;
	pid_t pid = spawn_piped(pipemenu->execute.c(), &pipe_fd);
	if (pid <= 0) {
		wlr_log(WLR_ERROR, "Failed to spawn pipe menu process %s",
			pipemenu->execute.c());
		return;
	}

	waiting_for_pipe_menu = true;
	auto ctx = new menu_pipe_context{.pipemenu = *pipemenu};
	ctx->pid = pid;
	ctx->pipe_fd = pipe_fd;
	ctx->buf = BUF_INIT;
	ctx->anchor_rect = anchor_rect;
	pipemenu->pipe_ctx.reset(ctx);

	ctx->event_read = wl_event_loop_add_fd(g_server.wl_event_loop, pipe_fd,
		WL_EVENT_READABLE, handle_pipemenu_readable, ctx);

	ctx->event_timeout = wl_event_loop_add_timer(g_server.wl_event_loop,
		handle_pipemenu_timeout, ctx);
	wl_event_source_timer_update(ctx->event_timeout, PIPEMENU_TIMEOUT_IN_MS);

	wlr_log(WLR_DEBUG, "[pipemenu %ld] executed: %s", (long)ctx->pid,
		ctx->pipemenu.execute.c());
}

static void
menu_process_item_selection(struct menuitem *item)
{
	assert(item);

	/* Do not keep selecting the same item */
	if (selected_item == item) {
		return;
	}

	if (waiting_for_pipe_menu) {
		return;
	}
	selected_item.reset(item);

	if (!item->selectable) {
		return;
	}

	/* We are on an item that has new focus */
	auto &menu = item->parent;
	menu_set_selection(&menu, item);
	if (menu.selection.menu) {
		/* Close old submenu tree */
		menu_close(menu.selection.menu.get());
	}

	if (CHECK_PTR(item->submenu, submenu)) {
		/* Sync the triggering view */
		submenu->triggered_by_view = menu.triggered_by_view;
		/* Ensure the submenu has its parent set correctly */
		submenu->parent = menu.parent;
		/* And open the new submenu tree */
		struct wlr_box anchor_rect = get_item_anchor_rect(item);
		if (submenu->execute && !submenu->scene_tree) {
			open_pipemenu_async(submenu, anchor_rect);
		} else {
			open_menu(submenu, anchor_rect);
		}
	}

	menu.selection.menu = item->submenu;
}

/* Get the deepest submenu with active item selection or the root menu itself */
static struct menu *
get_selection_leaf(void)
{
	struct menu *menu = g_server.menu_current.get();
	if (!menu) {
		return NULL;
	}

	for (CHECK_PTR(menu->selection.menu, sel); menu = sel) {
		if (!sel->selection.item) {
			return menu;
		}
	}

	return menu;
}

/* Selects the next or previous sibling of the currently selected item */
static void
menu_item_select(bool forward)
{
	struct menu *menu = get_selection_leaf();
	if (!menu) {
		return;
	}

	auto &items = menu->menuitems;
	auto start = forward ? items.begin() : items.rbegin();
	auto stop = forward ? items.end() : items.rend();
	auto next = lab::next_after_if(start, stop, menu->selection.item,
		/* wrap */ true, std::mem_fn(&menuitem::selectable));

	if (next) {
		menu_process_item_selection(next.get());
	}
}

static bool
menu_execute_item(struct menuitem *item)
{
	assert(item);

	if (item->submenu || !item->selectable) {
		/* We received a click on a separator or item that just opens a submenu */
		return false;
	}

	menu_close(g_server.menu_current.get());
	g_server.menu_current.reset();
	seat_focus_override_end();

	/*
	 * We call the actions after closing the menu so that virtual keyboard
	 * input is sent to the focused_surface instead of being absorbed by the
	 * menu. Consider for example: `wlrctl keyboard type abc`
	 *
	 * We cannot call menu_close_root() directly here because it does both
	 * menu_close() and destroy_pipemenus() which we have to handle
	 * before/after action_run() respectively.
	 */
	auto &menu = item->parent;
	if (menu.id == "client-list-combined-menu" && item->client_list_view) {
		actions_run(item->client_list_view, item->actions, NULL);
	} else {
		actions_run(menu.triggered_by_view, item->actions, NULL);
	}

	reset_pipemenus();
	return true;
}

/* Keyboard based selection */
void
menu_item_select_next(void)
{
	menu_item_select(/* forward */ true);
}

void
menu_item_select_previous(void)
{
	menu_item_select(/* forward */ false);
}

bool
menu_call_selected_actions(void)
{
	struct menu *menu = get_selection_leaf();
	if (!menu || !menu->selection.item) {
		return false;
	}

	return menu_execute_item(menu->selection.item.get());
}

/* Selects the first item on the submenu attached to the current selection */
void
menu_submenu_enter(void)
{
	struct menu *menu = get_selection_leaf(), *sel;
	if (!menu || !menu->selection.menu.check(sel)) {
		return;
	}

	auto iter = lab::find_if(sel->menuitems,
		std::mem_fn(&menuitem::selectable));
	if (iter) {
		menu_process_item_selection(iter.get());
	}
}

/* Re-selects the selected item on the parent menu of the current selection */
void
menu_submenu_leave(void)
{
	struct menu *menu = get_selection_leaf(), *parent;
	if (!menu || !menu->parent.check(parent) || !parent->selection.item) {
		return;
	}

	menu_process_item_selection(parent->selection.item.get());
}

/* Mouse based selection */
void
menu_process_cursor_motion(struct wlr_scene_node *node)
{
	assert(node && node->data);
	struct menuitem *item = node_menuitem_from_node(node);
	menu_process_item_selection(item);
}

void
menu_close_root(void)
{
	assert(g_server.input_mode == LAB_INPUT_STATE_MENU);
	assert(g_server.menu_current);

	menu_close(g_server.menu_current.get());
	g_server.menu_current.reset();
	reset_pipemenus();
	seat_focus_override_end();
}

void
menu_reconfigure(void)
{
	menu_finish();
	g_server.menu_current.reset();
	menu_init();
}
