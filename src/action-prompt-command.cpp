// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "action-prompt-command.h"
#include <wlr/util/log.h>
#include "action.h"
#include "common/buf.h"
#include "theme.h"
#include "translate.h"

lab_str
action_prompt_command(const char *format, action &action)
{
	if (!format) {
		wlr_log(WLR_ERROR, "missing format");
		return lab_str();
	}

	lab_str buf;
	for (const char *p = format; *p; p++) {
		/*
		 * If we're not on a conversion specifier (like %m) then just
		 * keep adding it to the buffer
		 */
		if (*p != '%') {
			buf += *p;
			continue;
		}

		/* Process the %* conversion specifier */
		++p;

		switch (*p) {
		case 'm': {
			buf += action.get_str("message.prompt",
				"Choose wisely");
			break;
		}
		case 'n':
			buf += _("No");
			break;
		case 'y':
			buf += _("Yes");
			break;
		case 'b':
			buf += hex_color_to_str(g_theme.osd_bg_color);
			break;
		case 't':
			buf += hex_color_to_str(g_theme.osd_label_text_color);
			break;
		default:
			wlr_log(WLR_ERROR,
				"invalid prompt command conversion specifier '%c'", *p);
			break;
		}
	}
	return buf;
}
