/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_ACTION_PROMPT_COMMAND_H
#define LABWC_ACTION_PROMPT_COMMAND_H

struct buf;
struct action;

void action_prompt_command(struct buf *buf, const char *format,
	struct action *action);

#endif /* LABWC_ACTION_PROMPT_COMMAND_H */
