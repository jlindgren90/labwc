/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_FOREIGN_TOPLEVEL_H
#define LABWC_FOREIGN_TOPLEVEL_H

#include <assert.h>
#include "foreign-toplevel/ext-foreign.h"
#include "foreign-toplevel/wlr-foreign.h"

struct view;

class foreign_toplevel
{
private:
	weakptr<wlr_foreign_toplevel> wlr_toplevel;
	weakptr<ext_foreign_toplevel> ext_toplevel;

public:
	foreign_toplevel(struct view *view) :
		wlr_toplevel(wlr_foreign_toplevel::create(view)),
		ext_toplevel(ext_foreign_toplevel::create(view)) {}

	~foreign_toplevel() {
		if (CHECK_PTR(wlr_toplevel, wt)) {
			wt->destroy();
			assert(!wlr_toplevel);
		}
		if (CHECK_PTR(ext_toplevel, et)) {
			et->destroy();
			assert(!ext_toplevel);
		}
	}

	void set_parent(foreign_toplevel &parent) {
		if (CHECK_PTR(wlr_toplevel, wt)) {
			wt->set_parent(parent.wlr_toplevel.get());
		}
	}
};

#endif /* LABWC_FOREIGN_TOPLEVEL_H */
