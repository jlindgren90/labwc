// SPDX-License-Identifier: GPL-2.0-only
#include "scaled-buffer/scaled-icon-buffer.h"
#include <wlr/util/log.h>
#include "buffer.h"
#include "config/rcxml.h"
#include "desktop-entry.h"
#include "img/img.h"
#include "labwc.h"
#include "view.h"

static struct lab_data_buffer *
choose_best_icon_buffer(struct view *view, int icon_size, float scale)
{
	int best_dist = -INT_MAX;
	struct lab_data_buffer *best_buffer = NULL;

	struct lab_data_buffer **buffer;
	wl_array_for_each(buffer, &view->icon.buffers) {
		int curr_dist = (*buffer)->base.width - (int)(icon_size * scale);
		bool curr_is_better;
		if ((curr_dist < 0 && best_dist > 0)
				|| (curr_dist > 0 && best_dist < 0)) {
			/* prefer too big icon over too small icon */
			curr_is_better = curr_dist > 0;
		} else {
			curr_is_better = abs(curr_dist) < abs(best_dist);
		}
		if (curr_is_better) {
			best_dist = curr_dist;
			best_buffer = *buffer;
		}
	}
	return best_buffer;
}

static struct lab_data_buffer *
img_to_buffer(struct lab_img *img, int icon_size, float scale)
{
	struct lab_data_buffer *buffer =
		lab_img_render(img, icon_size, icon_size, scale);
	lab_img_destroy(img);
	return buffer;
}

/*
 * Load an icon from application-supplied buffers.
 * X11 apps can provide icon buffers via _NET_WM_ICON property.
 */
static struct lab_data_buffer *
load_client_icon(struct view *view, int icon_size, float scale)
{
	struct lab_data_buffer *buffer =
		choose_best_icon_buffer(view, icon_size, scale);
	if (buffer) {
		wlr_log(WLR_DEBUG, "loaded icon from client buffer");
		return buffer_resize(buffer, icon_size, icon_size, scale);
	}

	return NULL;
}

/*
 * Load an icon by a view's app_id. For example, if the app_id is 'firefox', then
 * libsfdo will parse firefox.desktop to get the Icon name and then find that icon
 * based on the icon theme specified in rc.xml.
 */
static struct lab_data_buffer *
load_server_icon(struct view *view, int icon_size, float scale)
{
	struct lab_img *img = desktop_entry_load_icon_from_app_id(
		view->app_id, icon_size, scale);
	if (img) {
		wlr_log(WLR_DEBUG, "loaded icon by app_id");
		return img_to_buffer(img, icon_size, scale);
	}

	return NULL;
}

struct lab_data_buffer *
scaled_icon_buffer_load(struct view *view, int icon_size)
{
	float scale = g_server.max_output_scale;
	struct lab_img *img = NULL;
	struct lab_data_buffer *buffer = NULL;

	/* window icon */
	buffer = load_client_icon(view, icon_size, scale);
	if (buffer) {
		return buffer;
	}
	buffer = load_server_icon(view, icon_size, scale);
	if (buffer) {
		return buffer;
	}
	/* If both client and server icons are unavailable, use the fallback icon */
	img = desktop_entry_load_icon(rc.fallback_app_icon_name, icon_size, scale);
	if (img) {
		wlr_log(WLR_DEBUG, "loaded fallback icon");
		return img_to_buffer(img, icon_size, scale);
	}
	return NULL;
}
