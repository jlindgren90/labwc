// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdbool.h>
#include <strings.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include "common/macros.h"
#include "common/mem.h"
#include "config/libinput.h"
#include "config/rcxml.h"
#include "config/touch.h"
#include "input/ime.h"
#include "input/tablet.h"
#include "input/tablet-pad.h"
#include "input/input.h"
#include "input/keyboard.h"
#include "input/key-state.h"
#include "labwc.h"
#include "output.h"
#include "session-lock.h"
#include "view.h"

static void
input_device_destroy(struct wl_listener *listener, void *data)
{
	struct input *input = wl_container_of(listener, input, destroy);
	wl_list_remove(&input->link);
	wl_list_remove(&input->destroy.link);

	/* `struct keyboard` is derived and has some extra clean up to do */
	if (input->wlr_input_device->type == WLR_INPUT_DEVICE_KEYBOARD) {
		struct keyboard *keyboard = (struct keyboard *)input;
		wl_list_remove(&keyboard->key.link);
		wl_list_remove(&keyboard->modifiers.link);
		keyboard_cancel_keybind_repeat(keyboard);
	}
	free(input);
}

static enum lab_libinput_device_type
device_type_from_wlr_device(struct wlr_input_device *wlr_input_device)
{
	switch (wlr_input_device->type) {
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET:
		return LAB_LIBINPUT_DEVICE_TOUCH;
	default:
		break;
	}

	if (wlr_input_device->type == WLR_INPUT_DEVICE_POINTER &&
			wlr_input_device_is_libinput(wlr_input_device)) {
		struct libinput_device *libinput_device =
			wlr_libinput_get_device_handle(wlr_input_device);

		if (libinput_device_config_tap_get_finger_count(libinput_device) > 0) {
			return LAB_LIBINPUT_DEVICE_TOUCHPAD;
		}
	}

	return LAB_LIBINPUT_DEVICE_NON_TOUCH;
}

/*
 * Get applicable profile (category) by matching first by name and secondly be
 * type (e.g. 'touch' and 'non-touch'). If not suitable match is found based on
 * those two criteria we fallback on 'default'.
 */
static struct libinput_category *
get_category(struct wlr_input_device *device)
{
	/* By name */
	for (auto iter = rc.libinput_categories.rbegin(),
			end = rc.libinput_categories.rend();
			iter != end; iter++) {
		if (iter->name) {
			if (!strcasecmp(device->name, iter->name.c())) {
				return &*iter;
			}
		}
	}

	/* By type */
	enum lab_libinput_device_type type = device_type_from_wlr_device(device);
	for (auto iter = rc.libinput_categories.rbegin(),
			end = rc.libinput_categories.rend();
			iter != end; iter++) {
		if (iter->type == type) {
			return &*iter;
		}
	}

	/* Use default profile as a fallback */
	return libinput_category_get_default();
}

static void
configure_libinput(struct wlr_input_device *wlr_input_device)
{
	/*
	 * TODO: We do not check any return values for the various
	 *       libinput_device_config_*_set_*() calls. It would
	 *       be nice if we could inform the users via log file
	 *       that some libinput setting could not be applied.
	 *
	 * TODO: We are currently using int32_t with -1 as default
	 *       to describe the not-configured state. This is not
	 *       really optimal as we can't properly deal with
	 *       enum values that are 0. After some discussion via
	 *       IRC the best way forward seem to be to use a
	 *       uint32_t instead and UINT32_MAX as indicator for
	 *       a not-configured state. This allows to properly
	 *       test the enum being a member of a bitset via
	 *       mask & value == value. All libinput enums are
	 *       way below UINT32_MAX.
	 */

	if (!wlr_input_device) {
		wlr_log(WLR_ERROR, "no wlr_input_device");
		return;
	}
	struct input *input = wlr_input_device->data;

	/* Set scroll factor to 1.0 for Wayland/X11 backends or virtual pointers */
	if (!wlr_input_device_is_libinput(wlr_input_device)) {
		input->scroll_factor = 1.0;
		return;
	}

	struct libinput_device *libinput_dev =
		wlr_libinput_get_device_handle(wlr_input_device);
	if (!libinput_dev) {
		wlr_log(WLR_ERROR, "no libinput_dev");
		return;
	}

	struct libinput_category *dc = get_category(wlr_input_device);

	/*
	 * The above logic should have always matched SOME category
	 * (the default category if none other took precedence)
	 */
	assert(dc);

	wlr_log(WLR_INFO, "configuring input device %s (%s)",
		libinput_device_get_name(libinput_dev),
		libinput_device_get_sysname(libinput_dev));

	wlr_log(WLR_INFO, "matched category: %s",
		dc->name ? dc->name.c() : libinput_device_type_name(dc->type));

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0) {
		wlr_log(WLR_INFO, "tap unavailable");
	} else {
		wlr_log(WLR_INFO, "tap configured (tap=%d, button_map=%d)",
			dc->tap, dc->tap_button_map);
		libinput_device_config_tap_set_enabled(libinput_dev, dc->tap);
		libinput_device_config_tap_set_button_map(libinput_dev,
			dc->tap_button_map);
	}

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0
			|| dc->tap_and_drag < 0) {
		wlr_log(WLR_INFO, "tap-and-drag not configured");
	} else {
		wlr_log(WLR_INFO, "tap-and-drag configured (%d)",
			dc->tap_and_drag);
		libinput_device_config_tap_set_drag_enabled(
			libinput_dev, dc->tap_and_drag);
	}

	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0
			|| dc->drag_lock < 0) {
		wlr_log(WLR_INFO, "drag lock not configured");
	} else {
		wlr_log(WLR_INFO, "drag lock configured (%d)", dc->drag_lock);
		libinput_device_config_tap_set_drag_lock_enabled(
			libinput_dev, dc->drag_lock);
	}

#if HAVE_LIBINPUT_CONFIG_3FG_DRAG_ENABLED_3FG
	if (libinput_device_config_tap_get_finger_count(libinput_dev) <= 0
			|| dc->three_finger_drag < 0) {
		wlr_log(WLR_INFO, "three-finger drag not configured");
	} else {
		wlr_log(WLR_INFO, "three-finger drag configured (%d)",
			dc->three_finger_drag);
		libinput_device_config_3fg_drag_set_enabled(
			libinput_dev, dc->three_finger_drag);
	}
#endif

	if (libinput_device_config_scroll_has_natural_scroll(libinput_dev) <= 0
			|| dc->natural_scroll < 0) {
		wlr_log(WLR_INFO, "natural scroll not configured");
	} else {
		wlr_log(WLR_INFO, "natural scroll configured (%d)",
			dc->natural_scroll);
		libinput_device_config_scroll_set_natural_scroll_enabled(
			libinput_dev, dc->natural_scroll);
	}

	if (libinput_device_config_left_handed_is_available(libinput_dev) <= 0
			|| dc->left_handed < 0) {
		wlr_log(WLR_INFO, "left-handed mode not configured");
	} else {
		wlr_log(WLR_INFO, "left-handed mode configured (%d)",
			dc->left_handed);
		libinput_device_config_left_handed_set(libinput_dev,
			dc->left_handed);
	}

	if (libinput_device_config_accel_is_available(libinput_dev) == 0) {
		wlr_log(WLR_INFO, "pointer acceleration unavailable");
	} else {
		if (dc->pointer_speed >= -1) {
			wlr_log(WLR_INFO, "pointer speed configured (%g)",
				dc->pointer_speed);
			libinput_device_config_accel_set_speed(libinput_dev,
				dc->pointer_speed);
		} else {
			wlr_log(WLR_INFO, "pointer speed not configured");
		}

		if (dc->accel_profile > 0) {
			wlr_log(WLR_INFO,
				"pointer accel profile configured (%d)",
				dc->accel_profile);
			libinput_device_config_accel_set_profile(libinput_dev,
				dc->accel_profile);
		} else {
			wlr_log(WLR_INFO,
				"pointer accel profile not configured");
		}
	}

	if (libinput_device_config_middle_emulation_is_available(libinput_dev)
			== 0 || dc->middle_emu < 0)  {
		wlr_log(WLR_INFO, "middle emulation not configured");
	} else {
		wlr_log(WLR_INFO, "middle emulation configured (%d)",
			dc->middle_emu);
		libinput_device_config_middle_emulation_set_enabled(
			libinput_dev, dc->middle_emu);
	}

	if (libinput_device_config_dwt_is_available(libinput_dev) == 0
			|| dc->dwt < 0) {
		wlr_log(WLR_INFO, "dwt not configured");
	} else {
		wlr_log(WLR_INFO, "dwt configured (%d)", dc->dwt);
		libinput_device_config_dwt_set_enabled(libinput_dev, dc->dwt);
	}

	if ((dc->click_method != LIBINPUT_CONFIG_CLICK_METHOD_NONE
			&& (libinput_device_config_click_get_methods(libinput_dev)
				& dc->click_method) == 0)
			|| dc->click_method < 0) {
		wlr_log(WLR_INFO, "click method not configured");
	} else {
		wlr_log(WLR_INFO, "click method configured (%d)",
			dc->click_method);

		/*
		 * Note, the documentation claims that:
		 * > [...] The device may require changing to a neutral state
		 * > first before activating the new method.
		 *
		 * However, just setting the method seems to work without
		 * issues.
		 */

		libinput_device_config_click_set_method(libinput_dev, dc->click_method);
	}

	if (dc->scroll_method < 0) {
		wlr_log(WLR_INFO, "scroll method not configured");
	} else if (dc->scroll_method != LIBINPUT_CONFIG_SCROLL_NO_SCROLL
			&& (libinput_device_config_scroll_get_methods(libinput_dev)
				& dc->scroll_method) == 0) {
		wlr_log(WLR_INFO, "scroll method not supported");
	} else {
		wlr_log(WLR_INFO, "scroll method configured (%d)",
			dc->scroll_method);
		libinput_device_config_scroll_set_method(libinput_dev, dc->scroll_method);
	}

	if ((dc->send_events_mode != LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
			&& (libinput_device_config_send_events_get_modes(libinput_dev)
				& dc->send_events_mode) == 0)
			|| dc->send_events_mode < 0) {
		wlr_log(WLR_INFO, "send events mode not configured");
	} else {
		wlr_log(WLR_INFO, "send events mode configured (%d)",
			dc->send_events_mode);
		libinput_device_config_send_events_set_mode(libinput_dev, dc->send_events_mode);
	}

	/* Non-zero if the device can be calibrated, zero otherwise. */
	if (libinput_device_config_calibration_has_matrix(libinput_dev) == 0
			|| !dc->have_calibration_matrix) {
		wlr_log(WLR_INFO, "calibration matrix not configured");
	} else {
		wlr_log(WLR_INFO, "calibration matrix configured");
		libinput_device_config_calibration_set_matrix(libinput_dev, dc->calibration_matrix);
	}

	wlr_log(WLR_INFO, "scroll factor configured (%g)", dc->scroll_factor);
	input->scroll_factor = dc->scroll_factor;
}

static struct wlr_output *
output_by_name(const char *name)
{
	assert(name);
	struct output *output;
	wl_list_for_each(output, &g_server.outputs, link) {
		if (!strcasecmp(output->wlr_output->name, name)) {
			return output->wlr_output;
		}
	}
	return NULL;
}

static void
map_input_to_output(struct wlr_input_device *dev, const char *output_name)
{
	struct wlr_output *output = NULL;
	if (output_name) {
		output = output_by_name(output_name);
	}
	wlr_cursor_map_input_to_output(g_seat.cursor, dev, output);
	wlr_cursor_map_input_to_region(g_seat.cursor, dev, NULL);
}

static void
map_pointer_to_output(struct wlr_input_device *dev)
{
	struct wlr_pointer *pointer = wlr_pointer_from_input_device(dev);
	wlr_log(WLR_INFO, "map pointer to output %s", pointer->output_name);
	map_input_to_output(dev, pointer->output_name);
}

static struct input *
new_pointer(struct wlr_input_device *dev)
{
	struct input *input = znew(*input);
	input->wlr_input_device = dev;
	dev->data = input;
	configure_libinput(dev);
	wlr_cursor_attach_input_device(g_seat.cursor, dev);

	/* In support of running with WLR_WL_OUTPUTS set to >=2 */
	if (dev->type == WLR_INPUT_DEVICE_POINTER) {
		map_pointer_to_output(dev);
	}
	return input;
}

static struct input *
new_keyboard(struct wlr_input_device *device, bool is_virtual)
{
	struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);

	struct keyboard *keyboard = znew(*keyboard);
	keyboard->base.wlr_input_device = device;
	keyboard->wlr_keyboard = kb;
	keyboard->is_virtual = is_virtual;

	if (!g_seat.keyboard_group->keyboard.keymap) {
		wlr_log(WLR_ERROR, "cannot set keymap");
		exit(EXIT_FAILURE);
	}

	wlr_keyboard_set_keymap(kb, g_seat.keyboard_group->keyboard.keymap);

	/*
	 * This needs to be before wlr_keyboard_group_add_keyboard().
	 * For some reason, wlroots takes the modifier state from the
	 * new keyboard and syncs it to the others in the group, rather
	 * than the other way around.
	 */
	keyboard_set_numlock(kb);

	if (is_virtual) {
		/* key repeat information is usually synchronized via the keyboard group */
		wlr_keyboard_set_repeat_info(kb, rc.repeat_rate, rc.repeat_delay);
	} else {
		wlr_keyboard_group_add_keyboard(g_seat.keyboard_group, kb);
	}

	keyboard_setup_handlers(keyboard);

	wlr_seat_set_keyboard(g_seat.seat, kb);

	return (struct input *)keyboard;
}

static void
map_touch_to_output(struct wlr_input_device *dev)
{
	struct wlr_touch *touch = wlr_touch_from_input_device(dev);

	lab_str touch_config_output_name;
	struct touch_config_entry *config_entry =
		touch_find_config_for_device(touch->base.name);
	if (config_entry) {
		touch_config_output_name = config_entry->output_name;
	}

	const char *output_name = touch->output_name
		? touch->output_name : touch_config_output_name.c();
	wlr_log(WLR_INFO, "map touch to output %s", output_name ? output_name : "unknown");
	map_input_to_output(dev, output_name);
}

static struct input *
new_touch(struct wlr_input_device *dev)
{
	struct input *input = znew(*input);
	input->wlr_input_device = dev;
	dev->data = input;
	configure_libinput(dev);
	wlr_cursor_attach_input_device(g_seat.cursor, dev);
	/* In support of running with WLR_WL_OUTPUTS set to >=2 */
	map_touch_to_output(dev);

	return input;
}

static struct input *
new_tablet(struct wlr_input_device *dev)
{
	struct input *input = znew(*input);
	input->wlr_input_device = dev;
	tablet_create(dev);
	wlr_cursor_attach_input_device(g_seat.cursor, dev);
	wlr_log(WLR_INFO, "map tablet to output %s", rc.tablet.output_name.c());
	map_input_to_output(dev, rc.tablet.output_name.c());

	return input;
}

static struct input *
new_tablet_pad(struct wlr_input_device *dev)
{
	struct input *input = znew(*input);
	input->wlr_input_device = dev;
	tablet_pad_create(dev);

	return input;
}

static void
seat_update_capabilities(void)
{
	struct input *input = NULL;
	uint32_t caps = 0;

	wl_list_for_each(input, &g_seat.inputs, link) {
		switch (input->wlr_input_device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			caps |= WL_SEAT_CAPABILITY_KEYBOARD;
			break;
		case WLR_INPUT_DEVICE_POINTER:
		case WLR_INPUT_DEVICE_TABLET:
			caps |= WL_SEAT_CAPABILITY_POINTER;
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			caps |= WL_SEAT_CAPABILITY_TOUCH;
			break;
		default:
			break;
		}
	}
	wlr_seat_set_capabilities(g_seat.seat, caps);
}

static void
seat_add_device(struct input *input)
{
	input->destroy.notify = input_device_destroy;
	wl_signal_add(&input->wlr_input_device->events.destroy, &input->destroy);
	wl_list_insert(&g_seat.inputs, &input->link);

	seat_update_capabilities();
}

static void
handle_new_input(struct wl_listener *listener, void *data)
{
	struct wlr_input_device *device = data;
	struct input *input = NULL;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		input = new_keyboard(device, false);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		input = new_pointer(device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		input = new_touch(device);
		break;
	case WLR_INPUT_DEVICE_TABLET:
		input = new_tablet(device);
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		input = new_tablet_pad(device);
		break;
	default:
		wlr_log(WLR_INFO, "unsupported input device");
		return;
	}

	seat_add_device(input);
}

static void
new_virtual_pointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_virtual_pointer_v1 *pointer = event->new_pointer;
	struct wlr_input_device *device = &pointer->pointer.base;

	struct input *input = new_pointer(device);
	device->data = input;
	seat_add_device(input);
	if (event->suggested_output) {
		wlr_cursor_map_input_to_output(g_seat.cursor, device,
			event->suggested_output);
	}
}

static void
handle_new_virtual_keyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *virtual_keyboard = data;
	struct wlr_input_device *device = &virtual_keyboard->keyboard.base;

	struct input *input = new_keyboard(device, true);
	device->data = input;
	seat_add_device(input);
}

static void
handle_focus_change(struct wl_listener *listener, void *data)
{
	struct wlr_seat_keyboard_focus_change_event *event = data;
	struct wlr_surface *surface = event->new_surface;
	struct view *view = surface ? view_from_wlr_surface(surface) : NULL;

	/*
	 * Prevent focus switch to non-view surface (e.g. layer-shell
	 * or xwayland-unmanaged) from updating view state
	 */
	if (surface && !view) {
		return;
	}

	/*
	 * We clear the keyboard focus at the beginning of Move/Resize, window
	 * switcher and opening menus, but don't want to deactivate the view.
	 */
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	if (view != g_server.active_view) {
		if (g_server.active_view) {
			view_set_activated(g_server.active_view, false);
		}
		if (view) {
			view_set_activated(view, true);
			tablet_pad_enter_surface(surface);
		}
		g_server.active_view = view;
	}
}

void
seat_init(void)
{
	g_seat.seat = wlr_seat_create(g_server.wl_display, "seat0");
	if (!g_seat.seat) {
		wlr_log(WLR_ERROR, "cannot allocate seat");
		exit(EXIT_FAILURE);
	}

	wl_list_init(&g_seat.touch_points);
	wl_list_init(&g_seat.constraint_commit.link);
	wl_list_init(&g_seat.inputs);

	CONNECT_SIGNAL(g_server.backend, &g_seat, new_input);
	CONNECT_SIGNAL(&g_seat.seat->keyboard_state, &g_seat, focus_change);

	g_seat.virtual_pointer =
		wlr_virtual_pointer_manager_v1_create(g_server.wl_display);
	wl_signal_add(&g_seat.virtual_pointer->events.new_virtual_pointer,
		&g_seat.virtual_pointer_new);
	g_seat.virtual_pointer_new.notify = new_virtual_pointer;

	g_seat.virtual_keyboard =
		wlr_virtual_keyboard_manager_v1_create(g_server.wl_display);
	CONNECT_SIGNAL(g_seat.virtual_keyboard, &g_seat, new_virtual_keyboard);

	g_seat.input_method_relay = input_method_relay_create();

	g_seat.xcursor_manager = NULL;
	g_seat.cursor_visible = true;
	g_seat.cursor = wlr_cursor_create();
	if (!g_seat.cursor) {
		wlr_log(WLR_ERROR, "unable to create cursor");
		exit(EXIT_FAILURE);
	}
	wlr_cursor_attach_output_layout(g_seat.cursor, g_server.output_layout);

	wl_list_init(&g_seat.tablets);
	wl_list_init(&g_seat.tablet_tools);
	wl_list_init(&g_seat.tablet_pads);

	input_handlers_init();
}

void
seat_finish(void)
{
	wl_list_remove(&g_seat.new_input.link);
	wl_list_remove(&g_seat.focus_change.link);
	wl_list_remove(&g_seat.virtual_pointer_new.link);
	wl_list_remove(&g_seat.new_virtual_keyboard.link);

	struct input *input, *next;
	wl_list_for_each_safe(input, next, &g_seat.inputs, link) {
		input_device_destroy(&input->destroy, NULL);
	}

	if (g_seat.workspace_osd_timer) {
		wl_event_source_remove(g_seat.workspace_osd_timer);
		g_seat.workspace_osd_timer = NULL;
	}
	overlay_finish();

	input_handlers_finish();
	input_method_relay_finish(g_seat.input_method_relay);
}

static void
configure_keyboard(struct input *input)
{
	struct wlr_input_device *device = input->wlr_input_device;
	assert(device->type == WLR_INPUT_DEVICE_KEYBOARD);
	struct keyboard *keyboard = (struct keyboard *)input;
	struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
	keyboard_configure(kb, keyboard->is_virtual);
}

void
seat_pointer_end_grab(struct wlr_surface *surface)
{
	if (!surface || !wlr_seat_pointer_has_grab(g_seat.seat)) {
		return;
	}

	struct wlr_xdg_surface *xdg_surface =
		wlr_xdg_surface_try_from_wlr_surface(surface);
	if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_POPUP) {
		/*
		 * If we have an active popup grab (an open popup) and we are
		 * not on the popup itself, end that grab to close the popup.
		 * Contrary to pointer button notifications, a tablet/touch
		 * button notification sometimes doesn't end grabs automatically
		 * on button notifications in another client (observed in GTK4),
		 * so end the grab manually.
		 */
		wlr_seat_pointer_end_grab(g_seat.seat);
	}
}

/* This is called on SIGHUP (generally in response to labwc --reconfigure */
void
seat_reconfigure(void)
{
	struct input *input;
	cursor_reload();
	overlay_reconfigure();
	keyboard_reset_current_keybind();
	wl_list_for_each(input, &g_seat.inputs, link) {
		switch (input->wlr_input_device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			configure_keyboard(input);
			break;
		case WLR_INPUT_DEVICE_POINTER:
			configure_libinput(input->wlr_input_device);
			map_pointer_to_output(input->wlr_input_device);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			configure_libinput(input->wlr_input_device);
			map_touch_to_output(input->wlr_input_device);
			break;
		case WLR_INPUT_DEVICE_TABLET:
			map_input_to_output(input->wlr_input_device,
				rc.tablet.output_name.c());
			break;
		default:
			break;
		}
	}
}

static void
seat_focus(struct wlr_surface *surface, bool replace_exclusive_layer,
		bool is_lock_surface)
{
	/* Respect layer-shell exclusive keyboard-interactivity. */
	if (g_seat.focused_layer
			&& g_seat.focused_layer->current.keyboard_interactive
				== ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
			&& !replace_exclusive_layer) {
		return;
	}

	/*
	 * Respect session lock. This check is critical, DO NOT REMOVE.
	 * It should also come before the !surface condition, or the
	 * lock screen may lose focus and become impossible to unlock.
	 */
	if (g_server.session_lock_manager->locked && !is_lock_surface) {
		return;
	}

	if (!surface) {
		wlr_seat_keyboard_notify_clear_focus(g_seat.seat);
		input_method_relay_set_focus(g_seat.input_method_relay, NULL);
		return;
	}

	if (!wlr_seat_get_keyboard(g_seat.seat)) {
		/*
		 * wlr_seat_keyboard_notify_enter() sends wl_keyboard.modifiers,
		 * but it may crash some apps (e.g. Chromium) if
		 * wl_keyboard.keymap is not sent beforehand.
		 */
		wlr_seat_set_keyboard(g_seat.seat,
			&g_seat.keyboard_group->keyboard);
	}

	/*
	 * Key events associated with keybindings (both pressed and released)
	 * are not sent to clients. When changing surface-focus it is therefore
	 * important not to send the keycodes of _all_ pressed keys, but only
	 * those that were actually _sent_ to clients (that is, those that were
	 * not bound).
	 */
	uint32_t *pressed_sent_keycodes = key_state_pressed_sent_keycodes();
	int nr_pressed_sent_keycodes = key_state_nr_pressed_sent_keycodes();

	struct wlr_keyboard *kb = &g_seat.keyboard_group->keyboard;
	wlr_seat_keyboard_notify_enter(g_seat.seat, surface,
		pressed_sent_keycodes, nr_pressed_sent_keycodes,
		&kb->modifiers);

	input_method_relay_set_focus(g_seat.input_method_relay, surface);

	struct wlr_pointer_constraint_v1 *constraint =
		wlr_pointer_constraints_v1_constraint_for_surface(
			g_server.constraints, surface, g_seat.seat);
	constrain_cursor(constraint);
}

void
seat_focus_surface(struct wlr_surface *surface)
{
	/* Don't update focus while window switcher, Move/Resize and menu interaction */
	if (g_server.input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}
	seat_focus(surface, /*replace_exclusive_layer*/ false,
		/*is_lock_surface*/ false);
}

void
seat_focus_lock_surface(struct wlr_surface *surface)
{
	seat_focus(surface, /*replace_exclusive_layer*/ true,
		/*is_lock_surface*/ true);
}

void
seat_set_focus_layer(struct wlr_layer_surface_v1 *layer)
{
	if (!layer) {
		g_seat.focused_layer = NULL;
		desktop_focus_topmost_view();
		return;
	}
	seat_focus(layer->surface, /*replace_exclusive_layer*/ true,
		/*is_lock_surface*/ false);
	g_seat.focused_layer = layer;
}

static void
pressed_surface_destroy(struct wl_listener *listener, void *data)
{
	/*
	 * Using data directly prevents 'unused variable'
	 * warning when compiling without asserts
	 */
	assert(data == g_seat.pressed.surface);

	seat_reset_pressed();
}

void
seat_set_pressed(struct cursor_context *ctx)
{
	assert(ctx);
	assert(ctx->view || ctx->surface);
	seat_reset_pressed();

	g_seat.pressed = *ctx;

	if (ctx->surface) {
		g_seat.pressed_surface_destroy.notify = pressed_surface_destroy;
		wl_signal_add(&ctx->surface->events.destroy,
			&g_seat.pressed_surface_destroy);
	}
}

void
seat_reset_pressed(void)
{
	if (g_seat.pressed.surface) {
		wl_list_remove(&g_seat.pressed_surface_destroy.link);
	}
	g_seat.pressed = (struct cursor_context){0};
}

void
seat_output_layout_changed(void)
{
	struct input *input = NULL;
	wl_list_for_each(input, &g_seat.inputs, link) {
		switch (input->wlr_input_device->type) {
		case WLR_INPUT_DEVICE_POINTER:
			map_pointer_to_output(input->wlr_input_device);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			map_touch_to_output(input->wlr_input_device);
			break;
		case WLR_INPUT_DEVICE_TABLET:
			map_input_to_output(input->wlr_input_device,
				rc.tablet.output_name.c());
			break;
		default:
			break;
		}
	}
}

static void
handle_focus_override_surface_destroy(struct wl_listener *listener, void *data)
{
	wl_list_remove(&g_seat.focus_override.surface_destroy.link);
	g_seat.focus_override.surface = NULL;
}

void
seat_focus_override_begin(enum input_mode input_mode,
		enum lab_cursors cursor_shape)
{
	assert(!g_seat.focus_override.surface);
	assert(g_server.input_mode == LAB_INPUT_STATE_PASSTHROUGH);

	g_server.input_mode = input_mode;

	g_seat.focus_override.surface =
		g_seat.seat->keyboard_state.focused_surface;
	if (g_seat.focus_override.surface) {
		g_seat.focus_override.surface_destroy.notify =
			handle_focus_override_surface_destroy;
		wl_signal_add(&g_seat.focus_override.surface->events.destroy,
			&g_seat.focus_override.surface_destroy);
	}

	seat_focus(NULL, /*replace_exclusive_layer*/ false,
		/*is_lock_surface*/ false);
	wlr_seat_pointer_clear_focus(g_seat.seat);
	cursor_set(cursor_shape);
}

void
seat_focus_override_end(void)
{
	g_server.input_mode = LAB_INPUT_STATE_PASSTHROUGH;

	if (g_seat.focus_override.surface) {
		if (!g_seat.seat->keyboard_state.focused_surface) {
			seat_focus(g_seat.focus_override.surface,
				/*replace_exclusive_layer*/ false,
				/*is_lock_surface*/ false);
		}
		wl_list_remove(&g_seat.focus_override.surface_destroy.link);
		g_seat.focus_override.surface = NULL;
	}

	cursor_update_focus();
}
