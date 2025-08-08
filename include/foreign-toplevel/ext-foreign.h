/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_EXT_FOREIGN_TOPLEVEL_H
#define LABWC_EXT_FOREIGN_TOPLEVEL_H

#include "common/listener.h"
#include "common/refptr.h"

struct view;
struct wlr_ext_foreign_toplevel_handle_v1;

class ext_foreign_toplevel : public destroyable,
		public weak_target<ext_foreign_toplevel>
{
private:
	view *const m_view;
	wlr_ext_foreign_toplevel_handle_v1 *const m_handle;

	// private constructor, use create() instead
	ext_foreign_toplevel(view *view,
		wlr_ext_foreign_toplevel_handle_v1 *handle);

	/* Compositor side state updates */
	DECLARE_HANDLER(ext_foreign_toplevel, new_app_id);
	DECLARE_HANDLER(ext_foreign_toplevel, new_title);

public:
	static ext_foreign_toplevel *create(view *view);

	void destroy();
};

#endif /* LABWC_EXT_FOREIGN_TOPLEVEL_H */
