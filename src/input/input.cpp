// SPDX-License-Identifier: GPL-2.0-only
#include "input/input.h"
#include "input/cursor.h"
#include "input/keyboard.h"

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
