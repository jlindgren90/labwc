/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_ACTION_PROMPT_COMMAND_H
#define LABWC_ACTION_PROMPT_COMMAND_H

#include "common/str.h"

struct action;

lab_str action_prompt_command(const char *format, action &action);

#endif /* LABWC_ACTION_PROMPT_COMMAND_H */
