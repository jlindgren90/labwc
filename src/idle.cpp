// SPDX-License-Identifier: GPL-2.0-only

#include "idle.h"
#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include "common/listener.h"
#include "common/refptr.h"

struct lab_idle_inhibitor : public destroyable {
	wlr_idle_inhibitor_v1 *const wlr_inhibitor;

	lab_idle_inhibitor(wlr_idle_inhibitor_v1 *wlr_inhibitor)
		: wlr_inhibitor(wlr_inhibitor)
	{
		CONNECT_LISTENER(wlr_inhibitor, this, destroy);
	}

	~lab_idle_inhibitor();
};

struct lab_idle_manager : public destroyable,
		public weak_target<lab_idle_manager> {
	wlr_idle_notifier_v1 *const notifier;
	wlr_idle_inhibit_manager_v1 *const inhibit_mgr;

	lab_idle_manager(wl_display *display)
		: notifier(wlr_idle_notifier_v1_create(display)),
			inhibit_mgr(wlr_idle_inhibit_v1_create(display))
	{
		CONNECT_LISTENER(inhibit_mgr, this, new_inhibitor);
		CONNECT_LISTENER(inhibit_mgr, this, destroy);
	}

	DECLARE_HANDLER(lab_idle_manager, new_inhibitor);
};

static weakptr<lab_idle_manager> manager;

lab_idle_inhibitor::~lab_idle_inhibitor()
{
	if (CHECK_PTR(manager, mgr)) {
		/*
		 * The display destroy event might have been triggered
		 * already and thus the manager would be NULL.
		 */
		bool still_inhibited =
			wl_list_length(&mgr->inhibit_mgr->inhibitors) > 1;
		wlr_idle_notifier_v1_set_inhibited(mgr->notifier,
			still_inhibited);
	}
}

void
lab_idle_manager::handle_new_inhibitor(void *data)
{
	new lab_idle_inhibitor((wlr_idle_inhibitor_v1 *)data);
	wlr_idle_notifier_v1_set_inhibited(notifier, true);
}

void
idle_manager_create(struct wl_display *display)
{
	assert(!manager);
	manager.reset(new lab_idle_manager(display));
}

void
idle_manager_notify_activity(struct wlr_seat *seat)
{
	/*
	 * The display destroy event might have been triggered
	 * already and thus the manager would be NULL. Due to
	 * future code changes we might also get called before
	 * the manager has been created.
	 */
	if (CHECK_PTR(manager, mgr)) {
		wlr_idle_notifier_v1_notify_activity(mgr->notifier, seat);
	}
}
