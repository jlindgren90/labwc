// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "session-lock.h"
#include <assert.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include "common/mem.h"
#include "labwc.h"
#include "node.h"
#include "output.h"
#include "view.h"

struct session_lock : public destroyable, public weak_target<session_lock> {
	session_lock_manager *const manager;
	wlr_session_lock_v1 *const lock;

	session_lock(session_lock_manager *manager, wlr_session_lock_v1 *lock)
		: manager(manager), lock(lock)
	{
		CONNECT_LISTENER(lock, this, destroy);
		CONNECT_LISTENER(lock, this, new_surface);
		CONNECT_LISTENER(lock, this, unlock);
	}

	DECLARE_HANDLER(session_lock, new_surface);
	DECLARE_HANDLER(session_lock, unlock);
};

struct session_lock_surface : public destroyable,
		public weak_target<session_lock_surface>
{
	session_lock_output *const output;
	wlr_session_lock_surface_v1 *const surface;

	session_lock_surface(session_lock_output *output,
			wlr_session_lock_surface_v1 *surface)
		: output(output), surface(surface)
	{
		CONNECT_LISTENER(surface, this, destroy);
		CONNECT_LISTENER(surface->surface, this, map);
	}

	~session_lock_surface();

	DECLARE_HANDLER(session_lock_surface, map);
};

struct session_lock_output : public destroyable,
		public ref_guarded<session_lock_output>
{
	struct wlr_scene_tree *tree;
	struct wlr_scene_rect *background;
	struct session_lock_manager *manager;
	struct output *output;
	weak_owner<session_lock_surface> surface;
	struct wl_event_source *blank_timer;

	~session_lock_output();

	struct wlr_surface *wlr_surface() {
		CHECK_PTR_OR_RET_VAL(surface, s, nullptr);
		return s->surface->surface;
	}

	DECLARE_HANDLER(session_lock_output, commit);
};

static void
focus_surface(struct session_lock_manager *manager, struct wlr_surface *focused)
{
	manager->focused = focused;
	seat_focus_lock_surface(focused);
}

session_lock_surface::~session_lock_surface()
{
	/* Try to focus another session-lock surface */
	if (output->manager->focused != this->surface->surface) {
		return;
	}

	for (auto &iter : output->manager->lock_outputs) {
		if (&iter == output) {
			continue;
		}
		auto wlr_surface = iter.wlr_surface();
		if (wlr_surface && wlr_surface->mapped) {
			focus_surface(output->manager, wlr_surface);
			return;
		}
	}
	focus_surface(output->manager, NULL);
}

static void
update_focus(void *data)
{
	auto output = (session_lock_output *)data;
	cursor_update_focus();
	if (!output->manager->focused) {
		focus_surface(output->manager, output->wlr_surface());
	}
}

void
session_lock_surface::handle_map(void *)
{
	/*
	 * In order to update cursor shape on map, the call to
	 * cursor_update_focus() should be delayed because only the
	 * role-specific surface commit/map handler has been processed in
	 * wlroots at this moment and get_cursor_context() returns NULL as a
	 * buffer has not been actually attached to the surface.
	 */
	wl_event_loop_add_idle(g_server.wl_event_loop, update_focus, output);
}

static void
lock_output_reconfigure(struct session_lock_output *output)
{
	struct wlr_box box;
	wlr_output_layout_get_box(g_server.output_layout,
		output->output->wlr_output, &box);
	wlr_scene_rect_set_size(output->background, box.width, box.height);
	if (CHECK_PTR(output->surface, s)) {
		wlr_session_lock_surface_v1_configure(s->surface, box.width,
			box.height);
	}
}

static struct session_lock_output *
lock_output_for_output(struct session_lock_manager *manager,
		struct output *output)
{
	for (auto &lock_output : manager->lock_outputs) {
		if (lock_output.output == output) {
			return &lock_output;
		}
	}
	return NULL;
}

void
session_lock::handle_new_surface(void *data)
{
	auto lock_surface = (wlr_session_lock_surface_v1 *)data;
	auto output = (struct output *)lock_surface->output->data;
	struct session_lock_output *lock_output =
		lock_output_for_output(manager, output);
	if (!lock_output) {
		wlr_log(WLR_ERROR, "new lock surface, but no output");
		/* TODO: Consider improving security by handling this better */
		return;
	}

	struct wlr_scene_tree *surface_tree =
		wlr_scene_subsurface_tree_create(lock_output->tree, lock_surface->surface);
	die_if_null(surface_tree);

	node_descriptor_create(&surface_tree->node,
		LAB_NODE_SESSION_LOCK_SURFACE, /*view*/ NULL);

	lock_output->surface.reset(new session_lock_surface(lock_output,
		lock_surface));
	lock_output_reconfigure(lock_output);
}

session_lock_output::~session_lock_output()
{
	// cleanup of this->surface may focus another surface
	this->manager->lock_outputs.remove(this);
	wl_event_source_remove(this->blank_timer);
}

void
session_lock_output::handle_commit(void *data)
{
	auto event = (wlr_output_event_commit *)data;
	uint32_t require_reconfigure = WLR_OUTPUT_STATE_MODE
		| WLR_OUTPUT_STATE_SCALE | WLR_OUTPUT_STATE_TRANSFORM;
	if (event->state->committed & require_reconfigure) {
		lock_output_reconfigure(this);
	}
}

static void
align_session_lock_tree(struct output *output)
{
	struct wlr_box box;
	wlr_output_layout_get_box(g_server.output_layout, output->wlr_output, &box);
	wlr_scene_node_set_position(&output->session_lock_tree->node, box.x, box.y);
}

static int
handle_output_blank_timeout(void *data)
{
	auto lock_output = (session_lock_output *)data;
	wlr_scene_node_set_enabled(&lock_output->background->node, true);
	return 0;
}

void
session_lock_output_create(struct session_lock_manager *manager, struct output *output)
{
	if (lock_output_for_output(manager, output)) {
		return; /* already created */
	}

	auto lock_output = new session_lock_output();

	struct wlr_scene_tree *tree = wlr_scene_tree_create(output->session_lock_tree);
	die_if_null(tree);

	/*
	 * The ext-session-lock protocol says that the compositor should blank
	 * all outputs with an opaque color such that their normal content is
	 * fully hidden
	 */
	float black[4] = { 0.f, 0.f, 0.f, 1.f };
	struct wlr_scene_rect *background = wlr_scene_rect_create(tree, 0, 0, black);
	die_if_null(background);

	/*
	 * Delay blanking output by 100ms to prevent flicker. If the session is
	 * already locked, blank immediately.
	 */
	lock_output->blank_timer =
		wl_event_loop_add_timer(g_server.wl_event_loop,
			handle_output_blank_timeout, lock_output);
	if (!manager->locked) {
		wlr_scene_node_set_enabled(&background->node, false);
		wl_event_source_timer_update(lock_output->blank_timer, 100);
	}

	align_session_lock_tree(output);

	lock_output->output = output;
	lock_output->tree = tree;
	lock_output->background = background;
	lock_output->manager = manager;

	CONNECT_LISTENER(&tree->node, lock_output, destroy);
	CONNECT_LISTENER(output->wlr_output, lock_output, commit);

	lock_output_reconfigure(lock_output);

	manager->lock_outputs.append(lock_output);
}

static void
session_lock_destroy(struct session_lock_manager *manager)
{
	for (auto iter = manager->lock_outputs.begin(); iter;) {
		auto node = &iter->tree->node;
		++iter; // release ref held by iter
		wlr_scene_node_destroy(node);
	}
	manager->lock.reset();
}

void
session_lock::handle_unlock(void *)
{
	auto manager = this->manager;
	session_lock_destroy(manager); // deletes "this"
	manager->locked = false;

	if (CHECK_PTR(manager->last_active_view, view)) {
		desktop_focus_view(view, /* raise */ false);
	} else {
		desktop_focus_topmost_view();
	}
	manager->last_active_view.reset();

	cursor_update_focus();
}

void
session_lock_manager::handle_new_lock(void *data)
{
	auto manager = this;
	auto lock = (wlr_session_lock_v1 *)data;

	if (manager->lock) {
		wlr_log(WLR_ERROR, "session already locked");
		wlr_session_lock_v1_destroy(lock);
		return;
	}
	if (manager->locked) {
		wlr_log(WLR_INFO, "replacing abandoned lock");
		/* clear manager->lock_outputs */
		session_lock_destroy(manager);
	}
	assert(manager->lock_outputs.empty());

	/* Remember the focused view to restore it on unlock */
	manager->last_active_view = g_server.active_view;
	seat_focus_surface(NULL);

	for (auto &output : g_server.outputs) {
		session_lock_output_create(manager, &output);
	}

	manager->locked = true;
	manager->lock.reset(new session_lock(this, lock));
	wlr_session_lock_v1_send_locked(lock);
}

session_lock_manager::~session_lock_manager()
{
	session_lock_destroy(this);
	g_server.session_lock_manager = NULL;
}

void
session_lock_init(void)
{
	auto manager = new session_lock_manager();
	g_server.session_lock_manager = manager;
	manager->wlr_manager =
		wlr_session_lock_manager_v1_create(g_server.wl_display);

	CONNECT_LISTENER(manager->wlr_manager, manager, new_lock);
	CONNECT_LISTENER(manager->wlr_manager, manager, destroy);
}

void
session_lock_update_for_layout_change(void)
{
	if (!g_server.session_lock_manager->locked) {
		return;
	}

	for (auto &output : g_server.outputs) {
		align_session_lock_tree(&output);
	}

	struct session_lock_manager *manager = g_server.session_lock_manager;
	for (auto &lock_output : manager->lock_outputs) {
		lock_output_reconfigure(&lock_output);
	}
}
