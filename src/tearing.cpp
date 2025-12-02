// SPDX-License-Identifier: GPL-2.0-only

#include <wlr/types/wlr_tearing_control_v1.h>
#include "labwc.h"
#include "view.h"

struct tearing_controller : public destroyable {
	struct wlr_tearing_control_v1 *tearing_control;
	DECLARE_HANDLER(tearing_controller, set_hint);
};

void
tearing_controller::handle_set_hint(void *)
{
	struct view *view =
		view_from_wlr_surface(this->tearing_control->surface);
	if (view) {
		/*
		 * tearing_control->current is actually an enum:
		 * WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC = 0
		 * WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC = 1
		 *
		 * Using it as a bool here allows us to not ship the XML.
		 */
		view->tearing_hint = this->tearing_control->current;
	}
}

void
handle_tearing_new_object(struct wl_listener *listener, void *data)
{
	auto tearing_control = (wlr_tearing_control_v1 *)data;

	enum wp_tearing_control_v1_presentation_hint hint =
		wlr_tearing_control_manager_v1_surface_hint_from_surface(
			g_server.tearing_control, tearing_control->surface);
	wlr_log(WLR_DEBUG, "New presentation hint %d received for surface %p",
		hint, tearing_control->surface);

	auto controller = new tearing_controller();
	controller->tearing_control = tearing_control;

	CONNECT_LISTENER(tearing_control, controller, set_hint);
	CONNECT_LISTENER(tearing_control, controller, destroy);
}
