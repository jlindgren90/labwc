// SPDX-License-Identifier: GPL-2.0-only
#include "common/buf.h"
#include <ctype.h>
#include <string.h>
#include "common/string-helpers.h"

lab_str
buf_expand_tilde(const char *s)
{
	lab_str tmp;
	for (; *s; s++) {
		if (*s == '~') {
			auto home = getenv("HOME");
			tmp += home ? home : "";
		} else {
			tmp += *s;
		}
	}
	return tmp;
}

static void
strip_curly_braces(char *s)
{
	size_t len = strlen(s);
	if (s[0] != '{' || s[len - 1] != '}') {
		return;
	}
	len -= 2;
	memmove(s, s + 1, len);
	s[len] = 0;
}

static bool
isvalid(char p)
{
	return isalnum(p) || p == '_' || p == '{' || p == '}';
}

lab_str
buf_expand_shell_variables(const char *s)
{
	lab_str tmp;
	for (; *s; s++) {
		if (*s == '$' && isvalid(s[1])) {
			/* expand environment variable */
			lab_str envvar(&s[1]);
			char *p = envvar.data();
			while (isvalid(*p)) {
				++p;
			}
			*p = '\0';
			s += strlen(envvar.data());
			strip_curly_braces(envvar.data());
			p = getenv(envvar.data());
			if (p) {
				tmp += p;
			}
		} else {
			tmp += *s;
		}
	}
	return tmp;
}

lab_str
hex_color_to_str(const float color[4])
{
	/*
	 * In theme.c parse_hexstr() colors are pre-multiplied (by alpha) as
	 * expected by wlr_scene(). We therefore need to reverse that here.
	 *
	 * For details, see https://github.com/labwc/labwc/pull/1685
	 */
	float alpha = color[3];

	/* Avoid division by zero */
	if (alpha == 0.0f) {
		return lab_str("#00000000");
	}

	return strdup_printf("#%02x%02x%02x%02x",
		(int)(color[0] / alpha * 255),
		(int)(color[1] / alpha * 255),
		(int)(color[2] / alpha * 255),
		(int)(alpha * 255));
}
