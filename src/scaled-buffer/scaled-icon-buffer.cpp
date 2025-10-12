// SPDX-License-Identifier: GPL-2.0-only
#include "scaled-buffer/scaled-icon-buffer.h"
#include <wlr/util/log.h>
#include "config.h"
#include "config/rcxml.h"
#include "desktop-entry.h"
#include "img/img.h"
#include "node.h"
#include "view.h"
#include "window-rules.h"

#if HAVE_LIBSFDO

static refptr<lab_data_buffer>
choose_best_icon_buffer(struct scaled_icon_buffer *self, int icon_size,
		double scale)
{
	int best_dist = -INT_MAX;
	refptr<lab_data_buffer> best_buffer;

	for (auto &buffer : self->view_icon_buffers) {
		int curr_dist = buffer.width - (int)(icon_size * scale);
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
			best_buffer.reset(&buffer);
		}
	}
	return best_buffer;
}

/*
 * Load an icon from application-supplied icon name or buffers.
 * Wayland apps can provide icon names and buffers via xdg-toplevel-icon protocol.
 * X11 apps can provide icon buffers via _NET_WM_ICON property.
 */
static refptr<lab_data_buffer>
load_client_icon(struct scaled_icon_buffer *self, int icon_size, double scale)
{
	auto img = desktop_entry_load_icon(self->view_icon_name.c(), icon_size,
		scale);
	if (img.valid()) {
		wlr_log(WLR_DEBUG, "loaded icon from client icon name");
		return img.render(self->width, self->height, scale);
	}

	auto buffer = choose_best_icon_buffer(self, icon_size, scale);
	if (CHECK_PTR(buffer, buf)) {
		wlr_log(WLR_DEBUG, "loaded icon from client buffer");
		return buffer_scale_cairo_surface(buf->surface, self->width,
			self->height, scale);
	}

	return {};
}

/*
 * Load an icon by a view's app_id. For example, if the app_id is 'firefox', then
 * libsfdo will parse firefox.desktop to get the Icon name and then find that icon
 * based on the icon theme specified in rc.xml.
 */
static refptr<lab_data_buffer>
load_server_icon(struct scaled_icon_buffer *self, int icon_size, double scale)
{
	auto img = desktop_entry_load_icon_from_app_id(self->view_app_id.c(),
		icon_size, scale);
	if (img.valid()) {
		wlr_log(WLR_DEBUG, "loaded icon by app_id");
		return img.render(self->width, self->height, scale);
	}

	return {};
}

#endif /* HAVE_LIBSFDO */

refptr<lab_data_buffer>
scaled_icon_buffer::create_buffer(double scale)
{
#if HAVE_LIBSFDO
	auto self = this;
	int icon_size = MIN(self->width, self->height);

	if (self->icon_name) {
		/* generic icon (e.g. menu icons) */
		auto img = desktop_entry_load_icon(self->icon_name.c(),
			icon_size, scale);
		if (img.valid()) {
			wlr_log(WLR_DEBUG, "loaded icon by icon name");
			return img.render(self->width, self->height, scale);
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
	auto img = desktop_entry_load_icon(rc.fallback_app_icon_name.c(),
		icon_size, scale);
	if (img.valid()) {
		wlr_log(WLR_DEBUG, "loaded fallback icon");
		return img.render(self->width, self->height, scale);
	}
#endif /* HAVE_LIBSFDO */
	return {};
}

static bool
icon_buffers_equal(reflist<lab_data_buffer> &a, reflist<lab_data_buffer> &b)
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end(),
		[](lab_data_buffer &a, lab_data_buffer &b) {
			return &a == &b;
		});
}

bool
scaled_icon_buffer::equal(scaled_buffer &other)
{
	if (other.type != SCALED_ICON_BUFFER) {
		return false;
	}

	auto a = this;
	auto b = static_cast<scaled_icon_buffer *>(&other);

	return a->view_app_id == b->view_app_id
		&& a->view_icon_prefer_client == b->view_icon_prefer_client
		&& a->view_icon_name == b->view_icon_name
		&& icon_buffers_equal(a->view_icon_buffers,
			b->view_icon_buffers)
		&& a->icon_name == b->icon_name
		&& a->width == b->width
		&& a->height == b->height;
}

scaled_icon_buffer::scaled_icon_buffer(wlr_scene_tree *parent, int width,
		int height)
	: scaled_buffer(SCALED_ICON_BUFFER, parent)
{
	assert(width >= 0 && height >= 0);

	this->width = width;
	this->height = height;
}

void
scaled_icon_buffer::handle_set_icon(void *)
{
	CHECK_PTR_OR_RET(this->view, view);
	if (view_icon_name == view->icon.name
			&& icon_buffers_equal(view_icon_buffers, view->icon.buffers)) {
		return;
	}

	view_icon_name = view->icon.name;
	view_icon_buffers = view->icon.buffers;
	scaled_buffer_request_update(this, this->width, this->height);
}

void
scaled_icon_buffer::handle_new_title(void *)
{
	CHECK_PTR_OR_RET(this->view, view);
	bool prefer_client =
		window_rules_get_property(view, "iconPreferClient")
			== LAB_PROP_TRUE;
	if (prefer_client == view_icon_prefer_client) {
		return;
	}

	view_icon_prefer_client = prefer_client;
	scaled_buffer_request_update(this, this->width, this->height);
}

void
scaled_icon_buffer::handle_new_app_id(void *)
{
	CHECK_PTR_OR_RET(this->view, view);
	if (view->app_id == view_app_id) {
		return;
	}

	view_app_id = view->app_id;
	view_icon_prefer_client =
		window_rules_get_property(view, "iconPreferClient")
			== LAB_PROP_TRUE;
	scaled_buffer_request_update(this, this->width, this->height);
}

void
scaled_icon_buffer::handle_destroy(void *)
{
	// view was destroyed
	view.reset();
	on_new_app_id.disconnect();
	on_new_title.disconnect();
	on_set_icon.disconnect();
	on_destroy.disconnect();
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
	scaled_buffer_request_update(self, self->width, self->height);
}
