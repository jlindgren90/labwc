/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_KEYBIND_H
#define LABWC_KEYBIND_H

#include <xkbcommon/xkbcommon.h>
#include "action.h"
#include "common/alg.h"

struct keybind {
	uint32_t modifiers;
	std::vector<xkb_keysym_t> keysyms;
	bool use_syms_only;
	std::vector<xkb_keycode_t> keycodes;
	int keycodes_layout;
	bool allow_when_locked;
	std::vector<action> actions;
	bool on_release;
};

/**
 * keybind_create - parse keybind and add to linked list
 * @keybind: key combination
 */
keybind *keybind_append_new(std::vector<keybind> &keybinds,
	const char *keybind);

/**
 * parse_modifier - parse a string containing a single modifier name (e.g. "S")
 * into the represented modifier value. returns 0 for invalid modifier names.
 * @symname: modifier name
 */
uint32_t parse_modifier(const char *symname);

bool keybind_the_same(keybind &a, keybind &b);

bool keybind_contains_keycode(struct keybind *keybind, xkb_keycode_t keycode);
bool keybind_contains_keysym(struct keybind *keybind, xkb_keysym_t keysym);

void keybind_update_keycodes(void);

#endif /* LABWC_KEYBIND_H */
