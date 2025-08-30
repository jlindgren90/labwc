/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SESSION_LOCK_H
#define LABWC_SESSION_LOCK_H

#include <wayland-server-core.h>
#include "common/listener.h"
#include "common/reflist.h"

struct output;
struct session_lock;
struct session_lock_output;

struct session_lock_manager : public destroyable {
	struct wlr_session_lock_manager_v1 *wlr_manager;
	/* View re-focused on unlock */
	struct view *last_active_view;
	struct wlr_surface *focused;
	/*
	 * When not locked: lock=NULL, locked=false
	 * When locked: lock=non-NULL, locked=true
	 * When lock is destroyed without being unlocked: lock=NULL, locked=true
	 */
	weak_owner<session_lock> lock;
	bool locked;

	reflist<session_lock_output> lock_outputs;

	~session_lock_manager();

	DECLARE_HANDLER(session_lock_manager, new_lock);
};

void session_lock_init(void);
void session_lock_output_create(struct session_lock_manager *manager, struct output *output);
void session_lock_update_for_layout_change(void);

#endif /* LABWC_SESSION_LOCK_H */
