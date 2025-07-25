// SPDX-License-Identifier: LGPL-2.0-or-later
/*
 * XPM image loader adapted from gdk-pixbuf
 *
 * Copyright (C) 1999 Mark Crichton
 * Copyright (C) 1999 The Free Software Foundation
 *
 * Authors: Mark Crichton <crichton@gimp.org>
 *          Federico Mena-Quintero <federico@gimp.org>
 *
 * Adapted for labwc by John Lindgren, 2024
 */

#include "img/img-xpm.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <wlr/util/log.h>

#include "buffer.h"
#include "common/graphic-helpers.h"
#include "common/str.h"

enum buf_op { op_header, op_cmap, op_body };

static inline uint32_t
make_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static bool
parse_color(const char *spec, uint32_t *argb)
{
	if (spec[0] != '#') {
		return lookup_named_color(spec, argb);
	}

	int red, green, blue;
	switch (strlen(spec + 1)) {
	case 3:
		if (sscanf(spec + 1, "%1x%1x%1x", &red, &green, &blue) != 3) {
			return false;
		}
		*argb = make_argb(255, (red * 255) / 15, (green * 255) / 15,
			(blue * 255) / 15);
		return true;
	case 6:
		if (sscanf(spec + 1, "%2x%2x%2x", &red, &green, &blue) != 3) {
			return false;
		}
		*argb = make_argb(255, red, green, blue);
		return true;
	case 9:
		if (sscanf(spec + 1, "%3x%3x%3x", &red, &green, &blue) != 3) {
			return false;
		}
		*argb = make_argb(255, (red * 255) / 4095, (green * 255) / 4095,
			(blue * 255) / 4095);
		return true;
	case 12:
		if (sscanf(spec + 1, "%4x%4x%4x", &red, &green, &blue) != 3) {
			return false;
		}
		*argb = make_argb(255, (red * 255) / 65535,
			(green * 255) / 65535, (blue * 255) / 65535);
		return true;
	default:
		return false;
	}
}

static bool
xpm_seek_string(FILE *infile, const char *str)
{
	char instr[1024];

	while (!feof(infile)) {
		if (fscanf(infile, "%1023s", instr) < 0) {
			return false;
		}
		if (strcmp(instr, str) == 0) {
			return true;
		}
	}

	return false;
}

static bool
xpm_seek_char(FILE *infile, char c)
{
	int b, oldb;

	while ((b = getc(infile)) != EOF) {
		if (c != b && b == '/') {
			b = getc(infile);
			if (b == EOF) {
				return false;
			} else if (b == '*') { /* we have a comment */
				b = -1;
				do {
					oldb = b;
					b = getc(infile);
					if (b == EOF) {
						return false;
					}
				} while (!(oldb == '*' && b == '/'));
			}
		} else if (c == b) {
			return true;
		}
	}

	return false;
}

static lab_str
xpm_read_string(FILE *infile)
{
	lab_str buf;
	int c;

	do {
		c = getc(infile);
		if (c == EOF) {
			return lab_str();
		}
	} while (c != '"');

	while ((c = getc(infile)) != EOF) {
		if (c == '"') {
			return buf;
		}
		buf += (char)c;
	}

	return lab_str();
}

static uint32_t
xpm_extract_color(const char *buffer)
{
	const char *p = buffer;
	int new_key = 0;
	int key = 0;
	int current_key = 1;
	char word[129], color[129], current_color[129];
	char *r;

	word[0] = '\0';
	color[0] = '\0';
	current_color[0] = '\0';
	while (true) {
		/* skip whitespace */
		for (; *p != '\0' && g_ascii_isspace(*p); p++) {
			/* nothing */
		}
		/* copy word */
		for (r = word; *p != '\0' && !g_ascii_isspace(*p)
				&& r - word < (int)sizeof(word) - 1;
				p++, r++) {
			*r = *p;
		}
		*r = '\0';
		if (*word == '\0') {
			if (color[0] == '\0') { /* incomplete colormap entry */
				return 0;
			} else { /* end of entry, still store the last color */
				new_key = 1;
			}
		} else if (key > 0 && color[0] == '\0') {
			/* next word must be a color name part */
			new_key = 0;
		} else {
			if (strcmp(word, "c") == 0) {
				new_key = 5;
			} else if (strcmp(word, "g") == 0) {
				new_key = 4;
			} else if (strcmp(word, "g4") == 0) {
				new_key = 3;
			} else if (strcmp(word, "m") == 0) {
				new_key = 2;
			} else if (strcmp(word, "s") == 0) {
				new_key = 1;
			} else {
				new_key = 0;
			}
		}
		if (new_key == 0) {	/* word is a color name part */
			if (key == 0) { /* key expected */
				return 0;
			}
			/* accumulate color name */
			int len = strlen(color);
			if (len && len < (int)sizeof(color) - 1) {
				color[len++] = ' ';
			}
			g_strlcpy(color + len, word, sizeof(color) - len);
		} else { /* word is a key */
			if (key > current_key) {
				current_key = key;
				g_strlcpy(current_color, color, sizeof(current_color));
			}
			color[0] = '\0';
			key = new_key;
			if (*p == '\0') {
				break;
			}
		}
	}

	uint32_t argb;
	if (current_key > 1 && (g_ascii_strcasecmp(current_color, "None") != 0)
			&& parse_color(current_color, &argb)) {
		return argb;
	} else {
		return 0;
	}
}

static lab_str
file_buffer(enum buf_op op, FILE *infile)
{
	switch (op) {
	case op_header:
		if (!xpm_seek_string(infile, "XPM")) {
			break;
		}
		if (!xpm_seek_char(infile, '{')) {
			break;
		}
		/* Fall through to the next xpm_seek_char. */

	case op_cmap:
		xpm_seek_char(infile, '"');
		if (fseek(infile, -1, SEEK_CUR) != 0) {
			break;
		}
		/* Fall through to the xpm_read_string. */

	case op_body:
		return xpm_read_string(infile);

	default:
		g_assert_not_reached();
	}

	return lab_str();
}

static cairo_surface_t *
xpm_load_to_surface(FILE *infile)
{
	lab_str buffer = file_buffer(op_header, infile);
	if (!buffer) {
		wlr_log(WLR_DEBUG, "No XPM header found");
		return NULL;
	}

	int w, h, n_col, cpp, x_hot, y_hot;
	int items = sscanf(buffer.c(), "%d %d %d %d %d %d", &w, &h, &n_col,
		&cpp, &x_hot, &y_hot);

	if (items != 4 && items != 6) {
		wlr_log(WLR_DEBUG, "Invalid XPM header");
		return NULL;
	}

	if (w <= 0) {
		wlr_log(WLR_DEBUG, "XPM file has image width <= 0");
		return NULL;
	}
	if (h <= 0) {
		wlr_log(WLR_DEBUG, "XPM file has image height <= 0");
		return NULL;
	}
	/* Limits (width, height, colors) modified for labwc */
	if (h > 1024 || w > 1024) {
		wlr_log(WLR_DEBUG, "XPM file is larger than 1024x1024");
		return NULL;
	}
	if (cpp <= 0 || cpp >= 32) {
		wlr_log(WLR_DEBUG, "XPM has invalid number of chars per pixel");
		return NULL;
	}
	if (n_col <= 0 || n_col > 1024) {
		wlr_log(WLR_DEBUG, "XPM file has invalid number of colors");
		return NULL;
	}

	/* The hash is used for fast lookups of color from chars */
	std::unordered_map<std::string, uint32_t> color_map;
	uint32_t fallbackcolor = 0;

	for (int cnt = 0; cnt < n_col; cnt++) {
		buffer = file_buffer(op_cmap, infile);
		if (buffer.size() < (size_t)cpp) {
			wlr_log(WLR_DEBUG, "Cannot read XPM colormap");
			return NULL;
		}

		auto color_string = buffer.substr(0, cpp);
		uint32_t argb = xpm_extract_color(&buffer[cpp]);
		color_map.emplace(color_string, argb);

		if (cnt == 0) {
			fallbackcolor = argb;
		}
	}

	auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	uint32_t *data = (uint32_t *)cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface) / sizeof(uint32_t);

	for (int ycnt = 0; ycnt < h; ycnt++) {
		uint32_t *pixtmp = data + stride * ycnt;
		int wbytes = w * cpp;

		buffer = file_buffer(op_body, infile);
		if (buffer.size() < (size_t)wbytes) {
			/* Advertised width doesn't match pixels */
			wlr_log(WLR_DEBUG, "Dimensions do not match data");
			cairo_surface_destroy(surface);
			return NULL;
		}

		for (int n = 0, xcnt = 0; n < wbytes; n += cpp, xcnt++) {
			auto pixel_str = buffer.substr(n, cpp);
			auto iter = color_map.find(pixel_str);
			uint32_t argb = (iter == color_map.end())
				? fallbackcolor : iter->second;

			*pixtmp++ = argb;
		}
	}
	/* let cairo know pixel data has been modified */
	cairo_surface_mark_dirty(surface);
	return surface;
}

refptr<lab_data_buffer>
img_xpm_load(const char *filename)
{
	FILE *infile = fopen(filename, "rb");
	if (!infile) {
		wlr_log(WLR_ERROR, "error opening '%s'", filename);
		return {};
	}

	cairo_surface_t *surface = xpm_load_to_surface(infile);
	refptr<lab_data_buffer> buffer;
	if (surface) {
		buffer = buffer_adopt_cairo_surface(surface);
	} else {
		wlr_log(WLR_ERROR, "error loading '%s'", filename);
	}

	fclose(infile);
	return buffer;
}
