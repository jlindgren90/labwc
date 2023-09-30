/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_KEY_STATE_H
#define LABWC_KEY_STATE_H

/*
 * All keycodes in these functions are (Linux) libinput evdev scancodes which is
 * what 'wlr_keyboard' uses (e.g. 'seat->keyboard_group->keyboard->keycodes').
 * Note: These keycodes are different to XKB scancodes by a value of 8.
 */

/**
 * key_state_pressed_sent_keycodes - generate array of pressed+sent keys
 * Note: The array is generated by subtracting any bound keys from _all_ pressed
 * keys (because bound keys were not forwarded to clients).
 */
uint32_t *key_state_pressed_sent_keycodes(void);
int key_state_nr_pressed_sent_keycodes(void);

void key_state_set_pressed(uint32_t keycode, bool ispressed);
void key_state_store_pressed_keys_as_bound(void);
bool key_state_corresponding_press_event_was_bound(uint32_t keycode);
void key_state_bound_key_remove(uint32_t keycode);
int key_state_nr_bound_keys(void);
int key_state_nr_pressed_keys(void);

#endif /* LABWC_KEY_STATE_H */
