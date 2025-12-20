// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <ctype.h>
#include <wlr/util/log.h>
#include "common/buf.h"
#include "common/macros.h"
#include "config/rcxml.h"
#include "cycle.h"
#include "view.h"
#include "workspaces.h"
#include "labwc.h"
#include "desktop-entry.h"
#include "output.h"

/* includes '%', terminating 's' and NULL byte, 8 is enough for %-9999s */
#define LAB_FIELD_SINGLE_FMT_MAX_LEN 8
static_assert(LAB_FIELD_SINGLE_FMT_MAX_LEN <= 255, "fmt_position is a unsigned char");

/* forward declares */
typedef void field_conversion_type(lab_str &buf, struct view *view,
	const char *format);
struct field_converter {
	const char fmt_char;
	field_conversion_type *fn;
};

/* Internal helpers */

static const char *
get_identifier(struct view *view, bool trim)
{
	const char *identifier = view->app_id.c();

	/* remove the first two nodes of 'org.' strings */
	if (trim && !strncmp(identifier, "org.", 4)) {
		const char *p = identifier + 4;
		p = strchr(p, '.');
		if (p) {
			return ++p;
		}
	}
	return identifier;
}

static const char *
get_desktop_name(struct view *view)
{
#if HAVE_LIBSFDO
	const char *name = desktop_entry_name_lookup(view->app_id.c());
	if (name) {
		return name;
	}
#endif

	return get_identifier(view, /* trim */ true);
}

static const char *
get_type(struct view *view, bool short_form)
{
	switch (view->type) {
	case LAB_XDG_SHELL_VIEW:
		return short_form ? "[W]" : "[xdg-shell]";
#if HAVE_XWAYLAND
	case LAB_XWAYLAND_VIEW:
		return short_form ? "[X]" : "[xwayland]";
#endif
	}
	return "???";
}

static const char *
get_title_if_different(struct view *view)
{
	const char *identifier = get_identifier(view, /*trim*/ false);
	const char *title = view->title.c();
	return !strcmp(identifier, title) ? NULL : title;
}

/* Field handlers */
static void
field_set_type(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: B (backend) */
	buf += get_type(view, /*short_form*/ false);
}

static void
field_set_type_short(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: b (backend) */
	buf += get_type(view, /*short_form*/ true);
}

static void
field_set_workspace(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: W */
	buf += view->workspace->name;
}

static void
field_set_workspace_short(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: w */
	if (rc.workspace_config.names.size() > 1) {
		buf += view->workspace->name;
	}
}

static void
field_set_win_state(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: s */
	if (view->minimized) {
		buf += 'm';
	} else if (view->shaded) {
		buf += "s";
	} else if (view->maximized) {
		buf += "M";
	} else if (view->fullscreen) {
		buf += 'F';
	} else {
		buf += ' ';
	}
}

static void
field_set_win_state_all(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: S */
	buf += (view->minimized ? 'm' : ' ');
	buf += (view->shaded ? 's' : ' ');
	buf += (view->maximized ? 'M' : ' ');
	buf += (view->fullscreen ? 'F' : ' ');
	/* TODO: add always-on-top and omnipresent ? */
}

static void
field_set_output(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: O */
	if (output_is_usable(view->output)) {
		buf += view->output->wlr_output->name;
	}
}

static void
field_set_output_short(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: o */
	if (wl_list_length(&g_server.outputs) > 1
			&& output_is_usable(view->output)) {
		buf += view->output->wlr_output->name;
	}
}

static void
field_set_identifier(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: I */
	buf += get_identifier(view, /*trim*/ false);
}

static void
field_set_identifier_trimmed(lab_str &buf, struct view *view,
		const char *format)
{
	/* custom type conversion-specifier: i */
	buf += get_identifier(view, /*trim*/ true);
}

static void
field_set_desktop_entry_name(lab_str &buf, struct view *view,
		const char *format)
{
	/* custom type conversion-specifier: n */
	buf += get_desktop_name(view);
}

static void
field_set_title(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: T */
	buf += view->title;
}

static void
field_set_title_short(lab_str &buf, struct view *view, const char *format)
{
	/* custom type conversion-specifier: t */
	buf += get_title_if_different(view);
}

static const struct field_converter field_converter[] = {
	/* LAB_FIELD_NONE */               { '\0', NULL },
	/* LAB_FIELD_TYPE */               { 'B', field_set_type },
	/* LAB_FIELD_TYPE_SHORT */         { 'b', field_set_type_short },
	/* LAB_FIELD_IDENTIFIER */         { 'I', field_set_identifier },
	/* LAB_FIELD_TRIMMED_IDENTIFIER */ { 'i', field_set_identifier_trimmed },
	/* LAB_FIELD_ICON */               { '\0', NULL },
	/* LAB_FIELD_DESKTOP_ENTRY_NAME */ { 'n', field_set_desktop_entry_name},
	/* LAB_FIELD_TITLE */              { 'T', field_set_title },
	/* LAB_FIELD_TITLE_SHORT */        { 't', field_set_title_short },
	/* LAB_FIELD_WORKSPACE */          { 'W', field_set_workspace },
	/* LAB_FIELD_WORKSPACE_SHORT */    { 'w', field_set_workspace_short },
	/* LAB_FIELD_WIN_STATE */          { 's', field_set_win_state },
	/* LAB_FIELD_WIN_STATE_ALL */      { 'S', field_set_win_state_all },
	/* LAB_FIELD_OUTPUT */             { 'O', field_set_output },
	/* LAB_FIELD_OUTPUT_SHORT */       { 'o', field_set_output_short },
	/* fmt_char can never be matched so prevents LAB_FIELD_CUSTOM recursion */
	/* LAB_FIELD_CUSTOM */             { '\0', cycle_osd_field_set_custom },
};

static_assert(ARRAY_SIZE(field_converter) == LAB_FIELD_COUNT);

void
cycle_osd_field_set_custom(lab_str &buf, struct view *view, const char *format)
{
	if (!format) {
		wlr_log(WLR_ERROR, "Missing format for custom window switcher field");
		return;
	}

	char fmt[LAB_FIELD_SINGLE_FMT_MAX_LEN];
	unsigned char fmt_position = 0;

	lab_str field_result;
	char converted_field[4096];

	for (const char *p = format; *p; p++) {
		if (!fmt_position) {
			if (*p == '%') {
				fmt[fmt_position++] = *p;
			} else {
				/*
				 * Just relay anything not part of a
				 * format string to the output buffer.
				 */
				buf += *p;
			}
			continue;
		}

		/* Allow string formatting */
		/* TODO: add . for manual truncating? */
		/*
		 * Remove the || *p == '#' section as not used/needed
		 * change (*p >= '0' && *p <= '9') to isdigit(*p)
		 * changes by droc12345
		 */
		if (*p == '-' || isdigit(*p)) {
			if (fmt_position >= LAB_FIELD_SINGLE_FMT_MAX_LEN - 2) {
				/* Leave space for terminating 's' and NULL byte */
				wlr_log(WLR_ERROR,
					"single format string length exceeded: '%s'", p);
			} else {
				fmt[fmt_position++] = *p;
			}
			continue;
		}

		/* Handlers */
		for (unsigned char i = 0; i < LAB_FIELD_COUNT; i++) {
			if (*p != field_converter[i].fmt_char) {
				continue;
			}

			/* Generate the actual content*/
			field_converter[i].fn(field_result, view,
				/*format*/ NULL);

			/* Throw it at snprintf to allow formatting / padding */
			fmt[fmt_position++] = 's';
			fmt[fmt_position++] = '\0';
			snprintf(converted_field, sizeof(converted_field), fmt,
				field_result.c());

			/* And finally write it to the output buffer */
			buf += converted_field;
			goto reset_format;
		}

		wlr_log(WLR_ERROR,
			"invalid format character found for osd %s: '%c'",
			format, *p);

reset_format:
		/* Reset format string and tmp field result buffer */
		field_result.clear();
		fmt_position = 0;
	}
}

void
cycle_osd_field_arg_from_xml_node(struct cycle_osd_field *field,
		const char *nodename, const char *content)
{
	if (!strcmp(nodename, "content")) {
		if (!strcmp(content, "type")) {
			field->content = LAB_FIELD_TYPE;
		} else if (!strcmp(content, "type_short")) {
			field->content = LAB_FIELD_TYPE_SHORT;
		} else if (!strcmp(content, "app_id")) {
			wlr_log(WLR_ERROR, "window-switcher field 'app_id' is deprecated");
			field->content = LAB_FIELD_IDENTIFIER;
		} else if (!strcmp(content, "identifier")) {
			field->content = LAB_FIELD_IDENTIFIER;
		} else if (!strcmp(content, "trimmed_identifier")) {
			field->content = LAB_FIELD_TRIMMED_IDENTIFIER;
		} else if (!strcmp(content, "icon")) {
			field->content = LAB_FIELD_ICON;
		} else if (!strcmp(content, "desktop_entry_name")) {
			field->content = LAB_FIELD_DESKTOP_ENTRY_NAME;
		} else if (!strcmp(content, "title")) {
			field->content = LAB_FIELD_TITLE;
		} else if (!strcmp(content, "workspace")) {
			field->content = LAB_FIELD_WORKSPACE;
		} else if (!strcmp(content, "state")) {
			field->content = LAB_FIELD_WIN_STATE;
		} else if (!strcmp(content, "output")) {
			field->content = LAB_FIELD_OUTPUT;
		} else if (!strcmp(content, "custom")) {
			field->content = LAB_FIELD_CUSTOM;
		} else {
			wlr_log(WLR_ERROR, "bad windowSwitcher field '%s'", content);
		}
	} else if (!strcmp(nodename, "format")) {
		field->format = lab_str(content);
	} else if (!strcmp(nodename, "width") && !strchr(content, '%')) {
		wlr_log(WLR_ERROR, "Invalid osd field width: %s, misses trailing %%", content);
	} else if (!strcmp(nodename, "width")) {
		field->width = atoi(content);
	} else {
		wlr_log(WLR_ERROR, "Unexpected data in field parser: %s=\"%s\"",
			nodename, content);
	}
}

bool
cycle_osd_field_is_valid(struct cycle_osd_field *field)
{
	if (field->content == LAB_FIELD_NONE) {
		wlr_log(WLR_ERROR, "Invalid OSD field: no content set");
		return false;
	}
	if (field->content == LAB_FIELD_CUSTOM && !field->format) {
		wlr_log(WLR_ERROR, "Invalid OSD field: custom without format");
		return false;
	}
	if (!field->width) {
		wlr_log(WLR_ERROR, "Invalid OSD field: no width");
		return false;
	}
	return true;
}

lab_str
cycle_osd_field_get_content(struct cycle_osd_field *field, struct view *view)
{
	if (field->content == LAB_FIELD_NONE) {
		wlr_log(WLR_ERROR, "Invalid window switcher field type");
		return lab_str();
	}
	assert(field->content < LAB_FIELD_COUNT && field_converter[field->content].fn);

	lab_str buf;
	field_converter[field->content].fn(buf, view, field->format.c());
	return buf;
}
