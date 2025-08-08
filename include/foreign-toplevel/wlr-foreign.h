/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_WLR_FOREIGN_TOPLEVEL_H
#define LABWC_WLR_FOREIGN_TOPLEVEL_H

#include "common/listener.h"
#include "common/refptr.h"

struct view;
struct wlr_foreign_toplevel_handle_v1;

class wlr_foreign_toplevel : public destroyable,
		public weak_target<wlr_foreign_toplevel>
{
private:
	view *const m_view;
	wlr_foreign_toplevel_handle_v1 *const m_handle;

	// private constructor, use create() instead
	wlr_foreign_toplevel(view *view,
		wlr_foreign_toplevel_handle_v1 *handle);

	/* Client side events */
	DECLARE_HANDLER(wlr_foreign_toplevel, request_maximize);
	DECLARE_HANDLER(wlr_foreign_toplevel, request_minimize);
	DECLARE_HANDLER(wlr_foreign_toplevel, request_fullscreen);
	DECLARE_HANDLER(wlr_foreign_toplevel, request_activate);
	DECLARE_HANDLER(wlr_foreign_toplevel, request_close);

	/* Compositor side state updates */
	DECLARE_HANDLER(wlr_foreign_toplevel, new_app_id);
	DECLARE_HANDLER(wlr_foreign_toplevel, new_title);
	DECLARE_HANDLER(wlr_foreign_toplevel, new_outputs);
	DECLARE_HANDLER(wlr_foreign_toplevel, maximized);
	DECLARE_HANDLER(wlr_foreign_toplevel, minimized);
	DECLARE_HANDLER(wlr_foreign_toplevel, fullscreened);
	DECLARE_HANDLER(wlr_foreign_toplevel, activated);

public:
	static wlr_foreign_toplevel *create(view *view);

	void set_parent(wlr_foreign_toplevel *parent);
	void destroy();
};

#endif /* LABWC_WLR_FOREIGN_TOPLEVEL_H */
