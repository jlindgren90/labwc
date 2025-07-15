// SPDX-License-Identifier: GPL-2.0-only
#include "common/scaled-icon-buffer.h"
#include <wlr/util/log.h>
#include "config.h"
#include "config/rcxml.h"
#include "desktop-entry.h"
#include "img/img.h"
#include "node.h"
#include "view.h"
#include "window-rules.h"

#if HAVE_LIBSFDO

static lab_data_buffer_ptr
choose_best_icon_buffer(struct scaled_icon_buffer *self, int icon_size,
		double scale)
{
	int best_dist = INT_MIN;
	lab_data_buffer_ptr best_buffer;

	for (auto buffer : self->view_icon_buffers) {
		int curr_dist = buffer->width - (int)(icon_size * scale);
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
			best_buffer = buffer;
		}
	}
	return best_buffer;
}

static lab_data_buffer_ptr
img_to_buffer(struct lab_img *img, int width, int height, int scale)
{
	auto buffer = lab_img_render(img, width, height, scale);
	lab_img_destroy(img);
	return buffer;
}

/*
 * Load an icon from application-supplied icon name or buffers.
 * Wayland apps can provide icon names and buffers via xdg-toplevel-icon protocol.
 * X11 apps can provide icon buffers via _NET_WM_ICON property.
 */
static lab_data_buffer_ptr
load_client_icon(struct scaled_icon_buffer *self, int icon_size, double scale)
{
	struct lab_img *img = desktop_entry_load_icon(self->view_icon_name.c(),
		icon_size, scale);
	if (img) {
		wlr_log(WLR_DEBUG, "loaded icon from client icon name");
		return img_to_buffer(img, self->width, self->height, scale);
	}

	auto buffer = choose_best_icon_buffer(self, icon_size, scale);
	if (buffer) {
		wlr_log(WLR_DEBUG, "loaded icon from client buffer");
		return buffer_scale_cairo_surface(buffer->surface, self->width,
			self->height, scale);
	}

	return {};
}

/*
 * Load an icon by a view's app_id. For example, if the app_id is 'firefox', then
 * libsfdo will parse firefox.desktop to get the Icon name and then find that icon
 * based on the icon theme specified in rc.xml.
 */
static lab_data_buffer_ptr
load_server_icon(struct scaled_icon_buffer *self, int icon_size, double scale)
{
	struct lab_img *img =
		desktop_entry_load_icon_from_app_id(self->view_app_id.c(),
			icon_size, scale);
	if (img) {
		wlr_log(WLR_DEBUG, "loaded icon by app_id");
		return img_to_buffer(img, self->width, self->height, scale);
	}

	return {};
}

#endif /* HAVE_LIBSFDO */

lab_data_buffer_ptr
scaled_icon_buffer::create_buffer(double scale)
{
#if HAVE_LIBSFDO
	auto self = this;
	int icon_size = MIN(self->width, self->height);
	struct lab_img *img = NULL;

	if (self->icon_name) {
		/* generic icon (e.g. menu icons) */
		img = desktop_entry_load_icon(self->icon_name.c(), icon_size,
			scale);
		if (img) {
			wlr_log(WLR_DEBUG, "loaded icon by icon name");
			return img_to_buffer(img, self->width, self->height, scale);
		}
		return {};
	}

	/* window icon */
	if (self->view_icon_prefer_client) {
		auto buffer = load_client_icon(self, icon_size, scale);
		if (buffer) {
			return buffer;
		}
		buffer = load_server_icon(self, icon_size, scale);
		if (buffer) {
			return buffer;
		}
	} else {
		auto buffer = load_server_icon(self, icon_size, scale);
		if (buffer) {
			return buffer;
		}
		buffer = load_client_icon(self, icon_size, scale);
		if (buffer) {
			return buffer;
		}
	}
	/* If both client and server icons are unavailable, use the fallback icon */
	img = desktop_entry_load_icon(rc.fallback_app_icon_name, icon_size,
		scale);
	if (img) {
		wlr_log(WLR_DEBUG, "loaded fallback icon");
		return img_to_buffer(img, self->width, self->height, scale);
	}
#endif /* HAVE_LIBSFDO */
	return {};
}

static bool
lists_equal(reflist<lab_data_buffer> &a, reflist<lab_data_buffer> &b)
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

bool
scaled_icon_buffer::equal(scaled_scene_buffer &other)
{
	if (other.type != SCALED_ICON_BUFFER) {
		return false;
	}

	auto a = this;
	auto b = static_cast<scaled_icon_buffer *>(&other);

	return a->view_app_id == b->view_app_id
		&& a->view_icon_prefer_client == b->view_icon_prefer_client
		&& a->view_icon_name == b->view_icon_name
		&& lists_equal(a->view_icon_buffers, b->view_icon_buffers)
		&& a->icon_name == b->icon_name
		&& a->width == b->width
		&& a->height == b->height;
}

scaled_icon_buffer::scaled_icon_buffer(wlr_scene_tree *parent, int width,
		int height)
	: scaled_scene_buffer(SCALED_ICON_BUFFER, parent)
{
	assert(width >= 0 && height >= 0);

	this->width = width;
	this->height = height;
}

void
scaled_icon_buffer::handle_set_icon(void *)
{
	auto self = this;
	if (self->view_icon_name == self->view->icon.name
			&& lists_equal(self->view_icon_buffers,
				self->view->icon.buffers)) {
		return;
	}

	self->view_icon_name = self->view->icon.name;
	self->view_icon_buffers = self->view->icon.buffers;
	scaled_scene_buffer_request_update(self, self->width, self->height);
}

void
scaled_icon_buffer::handle_new_title(void *)
{
	auto self = this;
	bool prefer_client =
		window_rules_get_property(self->view.get(), "iconPreferClient")
			== LAB_PROP_TRUE;
	if (prefer_client == self->view_icon_prefer_client) {
		return;
	}
	self->view_icon_prefer_client = prefer_client;
	scaled_scene_buffer_request_update(self, self->width, self->height);
}

void
scaled_icon_buffer::handle_new_app_id(void *)
{
	auto self = this;
	const char *app_id = view_get_string_prop(self->view.get(), "app_id");
	if (app_id == self->view_app_id) {
		return;
	}

	self->view_app_id = lab_str(app_id);
	self->view_icon_prefer_client =
		window_rules_get_property(self->view.get(), "iconPreferClient")
			== LAB_PROP_TRUE;
	scaled_scene_buffer_request_update(self, self->width, self->height);
}

void
scaled_icon_buffer::handle_destroy(void *)
{
	// view was destroyed
	this->view.reset();
	this->on_new_app_id.disconnect();
	this->on_new_title.disconnect();
	this->on_set_icon.disconnect();
	this->on_destroy.disconnect();
}

void
scaled_icon_buffer_set_view(struct scaled_icon_buffer *self, struct view *view)
{
	assert(view);
	if (self->view == view) {
		return;
	}

	self->view.reset(view);
	CONNECT_LISTENER(view, self, new_app_id);
	CONNECT_LISTENER(view, self, new_title);
	CONNECT_LISTENER(view, self, set_icon);
	CONNECT_LISTENER(view, self, destroy);

	self->handle_new_app_id(NULL);
	self->handle_new_title(NULL);
	self->handle_set_icon(NULL);
}

void
scaled_icon_buffer_set_icon_name(struct scaled_icon_buffer *self,
	const char *icon_name)
{
	assert(icon_name);
	if (self->icon_name == icon_name) {
		return;
	}
	self->icon_name = lab_str(icon_name);
	scaled_scene_buffer_request_update(self, self->width, self->height);
}

struct scaled_icon_buffer *
scaled_icon_buffer_from_node(struct wlr_scene_node *node)
{
	struct scaled_scene_buffer *scaled_buffer =
		node_scaled_scene_buffer_from_node(node);
	assert(scaled_buffer->type == SCALED_ICON_BUFFER);
	return static_cast<scaled_icon_buffer *>(scaled_buffer);
}
