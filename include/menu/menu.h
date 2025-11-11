/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_MENU_H
#define LABWC_MENU_H

#include "action.h"
#include "common/reflist.h"

/* forward declare arguments */
struct menu;
struct menu_pipe_context;
struct view;
struct server;
struct wl_list;
struct wlr_scene_tree;
struct wlr_scene_node;
struct scaled_font_buffer;

enum menuitem_type {
	LAB_MENU_ITEM = 0,
	LAB_MENU_SEPARATOR_LINE,
	LAB_MENU_TITLE,
};

struct menuitem : public ref_guarded<menuitem>, public weak_target<menuitem> {
	menu &parent;
	std::vector<action> actions;
	lab_str text;
	lab_str icon_name;
	const char *arrow;
	weakptr<menu> submenu;
	bool selectable;
	enum menuitem_type type;
	int native_width;
	struct wlr_scene_tree *tree;
	struct wlr_scene_tree *normal_tree;
	struct wlr_scene_tree *selected_tree;
	weakptr<view> client_list_view; /* used by internal client-list */

	~menuitem();
};

/* This could be the root-menu or a submenu */
struct menu : public ref_guarded<menu>, public weak_target<menu> {
	lab_str id;
	lab_str label;
	lab_str icon_name;
	lab_str execute;
	weakptr<menu> parent;
	ownptr<menu_pipe_context> pipe_ctx;

	struct {
		int width;
		int height;
	} size;
	ownlist<menuitem> menuitems;
	struct {
		weakptr<::menu> menu;
		weakptr<menuitem> item;
	} selection;
	struct wlr_scene_tree *scene_tree;
	bool is_pipemenu_child;
	bool align_left;
	bool has_icons;

	/* Used to match a window-menu to the view that triggered it. */
	weakptr<view> triggered_by_view; /* may be NULL */

	~menu();
};

/* For keyboard support */
void menu_item_select_next(void);
void menu_item_select_previous(void);
void menu_submenu_enter(void);
void menu_submenu_leave(void);
bool menu_call_selected_actions(void);

void menu_init(void);
void menu_finish(void);
void menu_on_view_destroy(struct view *view);

/**
 * menu_get_by_id - get menu by id
 *
 * @id id string defined in menu.xml like "root-menu"
 */
struct menu *menu_get_by_id(const char *id);

/**
 * menu_open_root - open menu on position (x, y)
 *
 * This function will close server->menu_current, open the
 * new menu and assign @menu to server->menu_current.
 *
 * Additionally, server->input_mode will be set to LAB_INPUT_STATE_MENU.
 */
void menu_open_root(struct menu *menu, int x, int y);

/**
 * menu_process_cursor_motion
 *
 * - handles hover effects
 * - may open/close submenus
 */
void menu_process_cursor_motion(struct wlr_scene_node *node);

/**
 *  menu_close_root- close root menu
 *
 * This function will close server->menu_current and set it to NULL.
 * Asserts that server->input_mode is set to LAB_INPUT_STATE_MENU.
 *
 * Additionally, server->input_mode will be set to LAB_INPUT_STATE_PASSTHROUGH.
 */
void menu_close_root(void);

/* menu_reconfigure - reload theme and content */
void menu_reconfigure(void);

#endif /* LABWC_MENU_H */
