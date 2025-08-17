// SPDX-License-Identifier: GPL-2.0-only
#include "input/input.h"
#include "input/cursor.h"
#include "input/keyboard.h"
#include "labwc.h"

input::~input()
{
	g_seat.inputs.remove(this);
}

void
input_handlers_init(void)
{
	cursor_init();
	keyboard_group_init();
}

void
input_handlers_finish(void)
{
	cursor_finish();
	keyboard_group_finish();
}
