// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "action-prompt-command.h"
#include <wlr/util/log.h>
#include "action.h"
#include "common/buf.h"
#include "theme.h"
#include "translate.h"

void
action_prompt_command(struct buf *buf, const char *format,
		struct action *action, struct theme *theme)
{
	if (!format) {
		wlr_log(WLR_ERROR, "missing format");
		return;
	}

	for (const char *p = format; *p; p++) {
		/*
		 * If we're not on a conversion specifier (like %m) then just
		 * keep adding it to the buffer
		 */
		if (*p != '%') {
			buf_add_char(buf, *p);
			continue;
		}

		/* Process the %* conversion specifier */
		++p;

		switch (*p) {
		case 'm':
			buf_add(buf, action_get_str(action, "message.prompt",
				"Choose wisely"));
			break;
		case 'n':
			buf_add(buf, _("No"));
			break;
		case 'y':
			buf_add(buf, _("Yes"));
			break;
		case 'b':
			buf_add_hex_color(buf, theme->osd_bg_color);
			break;
		case 't':
			buf_add_hex_color(buf, theme->osd_label_text_color);
			break;
		default:
			wlr_log(WLR_ERROR,
				"invalid prompt command conversion specifier '%c'", *p);
			break;
		}
	}
}
