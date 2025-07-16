// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/keybind.h"
#include <assert.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_keyboard_group.h>
#include "config/rcxml.h"
#include "labwc.h"

uint32_t
parse_modifier(const char *symname)
{
	/* Mod2 == NumLock */
	if (!strcmp(symname, "S")) {
		return WLR_MODIFIER_SHIFT;
	} else if (!strcmp(symname, "C")) {
		return WLR_MODIFIER_CTRL;
	} else if (!strcmp(symname, "A") || !strcmp(symname, "Mod1")) {
		return WLR_MODIFIER_ALT;
	} else if (!strcmp(symname, "W") || !strcmp(symname, "Mod4")) {
		return WLR_MODIFIER_LOGO;
	} else if (!strcmp(symname, "M") || !strcmp(symname, "Mod5")) {
		return WLR_MODIFIER_MOD5;
	} else if (!strcmp(symname, "H") || !strcmp(symname, "Mod3")) {
		return WLR_MODIFIER_MOD3;
	} else {
		return 0;
	}
}

bool
keybind_the_same(keybind &a, keybind &b)
{
	return (a.modifiers == b.modifiers) && (a.keysyms == b.keysyms);
}

bool
keybind_contains_keycode(struct keybind *keybind, xkb_keycode_t keycode)
{
	assert(keybind);
	return lab::find(keybind->keycodes, keycode) != keybind->keycodes.end();
}

bool
keybind_contains_keysym(struct keybind *keybind, xkb_keysym_t keysym)
{
	assert(keybind);
	return lab::find(keybind->keysyms, keysym) != keybind->keysyms.end();
}

static bool
keybind_contains_any_keysym(struct keybind *keybind,
		const xkb_keysym_t *syms, int nr_syms)
{
	for (int i = 0; i < nr_syms; i++) {
		if (keybind_contains_keysym(keybind, syms[i])) {
			return true;
		}
	}
	return false;
}

static void
update_keycodes_iter(struct xkb_keymap *keymap, xkb_keycode_t key, void *data)
{
	const xkb_keysym_t *syms;
	xkb_layout_index_t layout = *(xkb_layout_index_t *)data;
	int nr_syms = xkb_keymap_key_get_syms_by_level(keymap, key, layout, 0, &syms);
	if (!nr_syms) {
		return;
	}
	for (auto &keybind : rc.keybinds) {
		if (keybind.keycodes_layout >= 0
				&& (xkb_layout_index_t)keybind.keycodes_layout
					!= layout) {
			/* Prevent storing keycodes from multiple layouts */
			continue;
		}
		if (keybind.use_syms_only) {
			continue;
		}
		if (keybind_contains_any_keysym(&keybind, syms, nr_syms)) {
			if (keybind_contains_keycode(&keybind, key)) {
				/* Prevent storing the same keycode twice */
				continue;
			}
			keybind.keycodes.push_back(key);
			keybind.keycodes_layout = layout;
		}
	}
}

void
keybind_update_keycodes(void)
{
	struct xkb_state *state = g_seat.keyboard_group->keyboard.xkb_state;
	struct xkb_keymap *keymap = xkb_state_get_keymap(state);

	for (auto &keybind : rc.keybinds) {
		keybind.keycodes.clear();
		keybind.keycodes_layout = -1;
	}
	xkb_layout_index_t layouts = xkb_keymap_num_layouts(keymap);
	for (xkb_layout_index_t i = 0; i < layouts; i++) {
		wlr_log(WLR_DEBUG, "Found layout %s", xkb_keymap_layout_get_name(keymap, i));
		xkb_keymap_key_for_each(keymap, update_keycodes_iter, &i);
	}
}

keybind *
keybind_append_new(std::vector<keybind> &keybinds, const char *keybind)
{
	::keybind k{};
	gchar **symnames = g_strsplit(keybind, "-", -1);
	for (size_t i = 0; symnames[i]; i++) {
		const char *symname = symnames[i];
		/*
		 * Since "-" is used as a separator, a keybind string like "W--"
		 * becomes "W", "", "". This means that it is impossible to bind
		 * an action to the "-" key in this way.
		 * We detect empty ""s outputted by g_strsplit and treat them as
		 * literal "-"s.
		 */
		if (!symname[0]) {
			/*
			 * You might have noticed that in the "W--" example, the
			 * output is "W", "", ""; which turns into "W", "-",
			 * "-". In order to avoid such duplications, we perform
			 * a lookahead on the tokens to treat that edge-case.
			 */
			if (symnames[i+1] && !symnames[i+1][0]) {
				continue;
			}
			symname = "-";
		}
		uint32_t modifier = parse_modifier(symname);
		if (modifier != 0) {
			k.modifiers |= modifier;
		} else {
			auto sym = xkb_keysym_from_name(symname,
				XKB_KEYSYM_CASE_INSENSITIVE);
			if (sym == XKB_KEY_NoSymbol && g_utf8_strlen(symname, -1) == 1) {
				/*
				 * xkb_keysym_from_name() only handles a legacy set of single
				 * characters. Thus we try to get the unicode codepoint here
				 * and try a direct translation instead.
				 *
				 * This allows using keybinds like 'W-ö' and similar.
				 */
				gunichar codepoint = g_utf8_get_char_validated(symname, -1);
				if (codepoint != (gunichar)-1) {
					sym = xkb_utf32_to_keysym(codepoint);
				}
			}
			sym = xkb_keysym_to_lower(sym);
			if (sym == XKB_KEY_NoSymbol) {
				wlr_log(WLR_ERROR, "unknown keybind (%s)", symname);
				g_strfreev(symnames);
				return nullptr;
			}
			k.keysyms.push_back(sym);
		}
	}
	g_strfreev(symnames);
	keybinds.push_back(std::move(k));
	return &keybinds.back();
}
