/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_XWAYLAND_H
#define LABWC_XWAYLAND_H
#include "config.h"

#if HAVE_XWAYLAND
#include "view.h"

struct wlr_compositor;
struct wlr_output;
struct wlr_output_layout;
struct wlr_scene_node;
struct wlr_xwayland_surface;

struct xwayland_unmanaged : public destroyable,
		public ref_guarded<xwayland_unmanaged>
{
	wlr_xwayland_surface *const xwayland_surface;
	wlr_scene_node *node = nullptr;

	// True if the surface has performed a keyboard grab. labwc
	// honors keyboard grabs and will give the surface focus when
	// it's mapped (which may occur slightly later) and on top.
	bool ever_grabbed_focus = false;

	xwayland_unmanaged(wlr_xwayland_surface *xsurface)
		: xwayland_surface(xsurface) {}

	DECLARE_HANDLER(xwayland_unmanaged, map);
	DECLARE_HANDLER(xwayland_unmanaged, unmap);
	DECLARE_HANDLER(xwayland_unmanaged, associate);
	DECLARE_HANDLER(xwayland_unmanaged, dissociate);
	DECLARE_HANDLER(xwayland_unmanaged, grab_focus);
	DECLARE_HANDLER(xwayland_unmanaged, request_activate);
	DECLARE_HANDLER(xwayland_unmanaged, request_configure);
/*	DECLARE_HANDLER(xwayland_unmanaged, request_fullscreen); */
	DECLARE_HANDLER(xwayland_unmanaged, set_geometry);
	DECLARE_HANDLER(xwayland_unmanaged, set_override_redirect);
};

struct xwayland_view : public view {
	wlr_xwayland_surface *const xwayland_surface;
	bool focused_before_map = false;

	xwayland_view(wlr_xwayland_surface *xsurface)
		: view(LAB_XWAYLAND_VIEW), xwayland_surface(xsurface) {}
	~xwayland_view();

	void configure(wlr_box geo) override;
	void close() override;
	void set_activated(bool activated) override;
	void set_fullscreen(bool fullscreen) override;
	void maximize(view_axis maximized) override;
	void minimize(bool minimize) override;
	view *get_parent() override;
	view *get_root() override;
	view_list get_children() override;
	bool is_modal_dialog() override;
	view_size_hints get_size_hints() override;
	enum view_wants_focus wants_focus() override;
	void offer_focus() override;
	bool has_strut_partial() override;
	bool contains_window_type(lab_window_type window_type) override;
	pid_t get_pid() override;

	void handle_map(void * = nullptr) override;
	void handle_unmap(void * = nullptr) override;
	void handle_commit(void *) override;
	void handle_request_move(void *) override;
	void handle_request_resize(void *) override;
	void handle_request_minimize(void *) override;
	void handle_request_maximize(void *) override;
	void handle_request_fullscreen(void *) override;
	void handle_set_title(void *) override;

	/* Events unique to XWayland views */
	DECLARE_HANDLER(xwayland_view, associate);
	DECLARE_HANDLER(xwayland_view, dissociate);
	DECLARE_HANDLER(xwayland_view, request_activate);
	DECLARE_HANDLER(xwayland_view, request_configure);
	DECLARE_HANDLER(xwayland_view, set_class);
	DECLARE_HANDLER(xwayland_view, set_decorations);
	DECLARE_HANDLER(xwayland_view, set_override_redirect);
	DECLARE_HANDLER(xwayland_view, set_strut_partial);
/*	DECLARE_HANDLER(xwayland_view, set_window_type); */
	DECLARE_HANDLER(xwayland_view, focus_in);
	DECLARE_HANDLER(xwayland_view, map_request);

	/* Not (yet) implemented */
/*	DECLARE_HANDLER(xwayland_view, set_role); */
/*	DECLARE_HANDLER(xwayland_view, set_hints); */
};

/* Global list of unmanaged surfaces */
extern reflist<xwayland_unmanaged> g_unmanaged_surfaces;

void xwayland_unmanaged_create(struct wlr_xwayland_surface *xsurface,
	bool mapped);

void xwayland_view_create(struct wlr_xwayland_surface *xsurface, bool mapped);

void xwayland_server_init(struct wlr_compositor *compositor);
void xwayland_server_finish(void);

void xwayland_adjust_usable_area(struct view *view,
	struct wlr_output_layout *layout, struct wlr_output *output,
	struct wlr_box *usable);

void xwayland_update_workarea(void);

void xwayland_reset_cursor(void);

void xwayland_flush(void);

#endif /* HAVE_XWAYLAND */
#endif /* LABWC_XWAYLAND_H */
