// SPDX-License-Identifier: GPL-2.0-only
#include <wlr/util/log.h>
#include "buffer.h"
#include "config/rcxml.h"
#include "desktop-entry.h"
#include "img/img.h"
#include "labwc.h"
#include "theme.h"
#include "view.h"

static struct lab_data_buffer *
img_to_buffer(struct lab_img *img, int icon_size, float scale)
{
	struct lab_data_buffer *buffer =
		lab_img_render(img, icon_size, icon_size, scale);
	lab_img_destroy(img);
	return buffer;
}

struct wlr_buffer *
scaled_icon_buffer_load(const char *app_id, cairo_surface_t *icon_surface)
{
	struct lab_img *img = NULL;
	struct lab_data_buffer *buffer = NULL;
	int icon_size = g_theme.window_icon_size;
	float scale = g_server.max_output_scale;

	/* window icon */
	if (icon_surface) {
		buffer = buffer_resize(icon_surface, icon_size, icon_size, scale);
		if (buffer) {
			return &buffer->base;
		}
	}
	/* icon lookup by app_id */
	img = desktop_entry_load_icon_from_app_id(app_id, icon_size, scale);
	if (img) {
		buffer = img_to_buffer(img, icon_size, scale);
		if (buffer) {
			return &buffer->base;
		}
	}
	/* If both client and server icons are unavailable, use the fallback icon */
	img = desktop_entry_load_icon(rc.fallback_app_icon_name, icon_size, scale);
	if (img) {
		buffer = img_to_buffer(img, icon_size, scale);
		if (buffer) {
			return &buffer->base;
		}
	}
	return NULL;
}
