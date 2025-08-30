// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>

#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#include "xwayland-shell-v1-protocol.h"
#endif

#include "common/macros.h"
#include "common/mem.h"
#include "common/scaled-scene-buffer.h"
#include "config/rcxml.h"
#include "config/session.h"
#include "decorations.h"
#include "desktop-entry.h"
#include "idle.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "layers.h"
#include "magnifier.h"
#include "menu/menu.h"
#include "output.h"
#include "output-virtual.h"
#include "regions.h"
#include "resize-indicator.h"
#include "session-lock.h"
#include "ssd.h"
#include "theme.h"
#include "view.h"
#include "workspaces.h"
#include "xwayland.h"

#define LAB_EXT_DATA_CONTROL_VERSION 1
#define LAB_EXT_FOREIGN_TOPLEVEL_LIST_VERSION 1
#define LAB_WLR_COMPOSITOR_VERSION 6
#define LAB_WLR_FRACTIONAL_SCALE_V1_VERSION 1
#define LAB_WLR_LINUX_DMABUF_VERSION 4
#define LAB_WLR_PRESENTATION_TIME_VERSION 2

static void
reload_config_and_theme(void)
{
	scaled_scene_buffer_invalidate_sharing();
	rcxml_finish();
	rcxml_read(rc.config_file);
	theme_finish();
	theme_init(rc.theme_name);

#if HAVE_LIBSFDO
	desktop_entry_finish();
	desktop_entry_init();
#endif

	struct view *view;
	wl_list_for_each(view, &g_server.views, link) {
		view_reload_ssd(view);
	}

	menu_reconfigure();
	seat_reconfigure();
	regions_reconfigure();
	resize_indicator_reconfigure();
	kde_server_decoration_update_default();
	workspaces_reconfigure();
}

static int
handle_sighup(int signal, void *data)
{
	keyboard_cancel_all_keybind_repeats();
	session_environment_init();
	reload_config_and_theme();
	output_virtual_update_fallback();
	return 0;
}

static int
handle_sigterm(int signal, void *data)
{
	struct wl_display *display = data;

	wl_display_terminate(display);
	return 0;
}

static int
handle_sigchld(int signal, void *data)
{
	siginfo_t info;
	info.si_pid = 0;

	/* First call waitid() with NOWAIT which doesn't consume the zombie */
	if (waitid(P_ALL, /*id*/ 0, &info, WEXITED | WNOHANG | WNOWAIT) == -1) {
		return 0;
	}

	if (info.si_pid == 0) {
		/* No children in waitable state */
		return 0;
	}

#if HAVE_XWAYLAND
	/* Ensure that we do not break xwayland lazy initialization */
	if (g_server.xwayland && g_server.xwayland->server
			&& info.si_pid == g_server.xwayland->server->pid) {
		return 0;
	}
#endif

	/* And then do the actual (consuming) lookup again */
	int ret = waitid(P_PID, info.si_pid, &info, WEXITED);
	if (ret == -1) {
		wlr_log(WLR_ERROR, "blocking waitid() for %ld failed: %d",
			(long)info.si_pid, ret);
		return 0;
	}

	const char *signame;
	switch (info.si_code) {
	case CLD_EXITED:
		wlr_log(info.si_status == 0 ? WLR_DEBUG : WLR_ERROR,
			"spawned child %ld exited with %d",
			(long)info.si_pid, info.si_status);
		break;
	case CLD_KILLED:
	case CLD_DUMPED:
		signame = strsignal(info.si_status);
		wlr_log(WLR_ERROR,
			"spawned child %ld terminated with signal %d (%s)",
				(long)info.si_pid, info.si_status,
				signame ? signame : "unknown");
		break;
	default:
		wlr_log(WLR_ERROR,
			"spawned child %ld terminated unexpectedly: %d"
			" please report", (long)info.si_pid, info.si_code);
	}

	if (info.si_pid == g_server.primary_client_pid) {
		wlr_log(WLR_INFO, "primary client %ld exited", (long)info.si_pid);
		wl_display_terminate(g_server.wl_display);
	}

	return 0;
}

static void
handle_drm_lease_request(struct wl_listener *listener, void *data)
{
	struct wlr_drm_lease_request_v1 *req = data;
	struct wlr_drm_lease_v1 *lease = wlr_drm_lease_request_v1_grant(req);
	if (!lease) {
		wlr_log(WLR_ERROR, "Failed to grant lease request");
		wlr_drm_lease_request_v1_reject(req);
		return;
	}
}

static bool
protocol_is_privileged(const struct wl_interface *iface)
{
	static const char * const rejected[] = {
		"wp_drm_lease_device_v1",
		"zwlr_gamma_control_manager_v1",
		"zwlr_output_manager_v1",
		"zwlr_output_power_manager_v1",
		"zwp_input_method_manager_v2",
		"zwlr_virtual_pointer_manager_v1",
		"zwp_virtual_keyboard_manager_v1",
		"zwlr_export_dmabuf_manager_v1",
		"zwlr_screencopy_manager_v1",
		"ext_data_control_manager_v1",
		"zwlr_data_control_manager_v1",
		"wp_security_context_manager_v1",
		"ext_idle_notifier_v1",
		"zcosmic_workspace_manager_v1",
		"zwlr_foreign_toplevel_manager_v1",
		"ext_foreign_toplevel_list_v1",
		"ext_session_lock_manager_v1",
		"zwlr_layer_shell_v1",
		"ext_workspace_manager_v1",
		"ext_image_copy_capture_manager_v1",
		"ext_output_image_capture_source_manager_v1",
	};
	for (size_t i = 0; i < ARRAY_SIZE(rejected); i++) {
		if (!strcmp(iface->name, rejected[i])) {
			return true;
		}
	}
	return false;
}

static bool
allow_for_sandbox(const struct wlr_security_context_v1_state *security_state,
		const struct wl_interface *iface)
{
	if (!strcmp(iface->name, "security_context_manager_v1")) {
		return false;
	}

	/* protocols are split into 3 blocks, from least privileges to highest privileges */
	static const char * const allowed_protocols[] = {
		/* absolute base */
		"wl_shm",
		"wl_compositor",
		"wl_subcompositor",
		"wl_data_device_manager", /* would be great if we could drop this one */
		"wl_seat",
		"xdg_wm_base",
		/* enhanced */
		"wl_output",
		"wl_drm",
		"zwp_linux_dmabuf_v1",
		"zwp_primary_selection_device_manager_v1",
		"zwp_text_input_manager_v3",
		"zwp_pointer_gestures_v1",
		"wp_cursor_shape_manager_v1",
		"zwp_relative_pointer_manager_v1",
		"xdg_activation_v1",
		"org_kde_kwin_server_decoration_manager",
		"zxdg_decoration_manager_v1",
		"wp_presentation",
		"wp_viewporter",
		"wp_single_pixel_buffer_manager_v1",
		"wp_fractional_scale_manager_v1",
		"wp_tearing_control_manager_v1",
		"zwp_tablet_manager_v2",
		"zxdg_importer_v1",
		"zxdg_importer_v2",
		"xdg_toplevel_icon_manager_v1",
		/* plus */
		"wp_alpha_modifier_v1",
		"wp_linux_drm_syncobj_manager_v1",
		"zxdg_exporter_v1",
		"zxdg_exporter_v2",
		"zwp_idle_inhibit_manager_v1",
		"zwp_pointer_constraints_v1",
		"zxdg_output_manager_v1",
	};

	for (size_t i = 0; i < ARRAY_SIZE(allowed_protocols); i++) {
		if (!strcmp(iface->name, allowed_protocols[i])) {
			return true;
		}
	}
	return false;
}

static bool
server_global_filter(const struct wl_client *client, const struct wl_global *global, void *data)
{
	const struct wl_interface *iface = wl_global_get_interface(global);
	/* Silence unused var compiler warnings */
	(void)iface;

#if HAVE_XWAYLAND
	struct wl_client *xwayland_client =
		(g_server.xwayland && g_server.xwayland->server)
			? g_server.xwayland->server->client
			: NULL;

	if (client != xwayland_client && !strcmp(iface->name, xwayland_shell_v1_interface.name)) {
		/* Filter out the xwayland shell for usual clients */
		return false;
	}
#endif

	/* Do not allow security_context_manager_v1 to clients with a security context attached */
	const struct wlr_security_context_v1_state *security_context =
		wlr_security_context_manager_v1_lookup_client(
			g_server.security_context_manager_v1,
			(struct wl_client *)client);
	if (security_context && global
			== g_server.security_context_manager_v1->global) {
		return false;
	} else if (security_context) {
		/*
		 * We are using an allow list for sandboxes to not
		 * accidentally leak a new privileged protocol.
		 */
		bool allow = allow_for_sandbox(security_context, iface);
		/*
		 * TODO: The following call is basically useless right now
		 *       and should be replaced with
		 *       assert(allow || protocol_is_privileged(iface));
		 *       This ensures that our lists are in sync with what
		 *       protocols labwc supports.
		 */
		if (!allow && !protocol_is_privileged(iface)) {
			wlr_log(WLR_ERROR, "Blocking unknown protocol %s", iface->name);
		} else if (!allow) {
			wlr_log(WLR_DEBUG, "Blocking %s for security context %s->%s->%s",
				iface->name, security_context->sandbox_engine,
				security_context->app_id, security_context->instance_id);
		}
		return allow;
	}

	return true;
}

/*
 * This message is intended to help users who are trying labwc on
 * clean/minimalist systems without existing Desktop Environments (possibly
 * through Virtual Managers) where polkit is missing or GPU drivers do not
 * exist, in the hope that it will reduce the time required to get labwc running
 * and prevent some troubleshooting steps.
 */
static const char helpful_seat_error_message[] =
"\n"
"Some friendly trouble-shooting help\n"
"===================================\n"
"\n"
"If a seat could not be created, this may be caused by lack of permission to the\n"
"seat, input and video groups. If you are using a systemd setup, try installing\n"
"polkit (sometimes called policykit-1). For other setups, search your OS/Distro's\n"
"documentation on how to use seatd, elogind or similar. This is likely to involve\n"
"manually adding users to groups.\n"
"\n"
"If the above does not work, try running with `WLR_RENDERER=pixman labwc` in\n"
"order to use the software rendering fallback\n";

static void
get_headless_backend(struct wlr_backend *backend, void *data)
{
	if (wlr_backend_is_headless(backend)) {
		struct wlr_backend **headless = data;
		*headless = backend;
	}
}

static void
handle_renderer_lost(struct wl_listener *listener, void *data)
{
	wlr_log(WLR_INFO, "Re-creating renderer after GPU reset");

	struct wlr_renderer *renderer =
		wlr_renderer_autocreate(g_server.backend);
	if (!renderer) {
		wlr_log(WLR_ERROR, "Unable to create renderer");
		return;
	}

	struct wlr_allocator *allocator =
		wlr_allocator_autocreate(g_server.backend, renderer);
	if (!allocator) {
		wlr_log(WLR_ERROR, "Unable to create allocator");
		wlr_renderer_destroy(renderer);
		return;
	}

	struct wlr_renderer *old_renderer = g_server.renderer;
	struct wlr_allocator *old_allocator = g_server.allocator;
	g_server.renderer = renderer;
	g_server.allocator = allocator;

	wl_list_remove(&g_server.renderer_lost.link);
	wl_signal_add(&g_server.renderer->events.lost, &g_server.renderer_lost);

	wlr_compositor_set_renderer(g_server.compositor, renderer);

	struct output *output;
	wl_list_for_each(output, &g_server.outputs, link) {
		wlr_output_init_render(output->wlr_output, g_server.allocator,
			g_server.renderer);
	}

	reload_config_and_theme();

	magnifier_reset();

	wlr_allocator_destroy(old_allocator);
	wlr_renderer_destroy(old_renderer);
}

void
server_init(void)
{
	g_server.primary_client_pid = -1;
	g_server.wl_display = wl_display_create();
	if (!g_server.wl_display) {
		wlr_log(WLR_ERROR, "cannot allocate a wayland display");
		exit(EXIT_FAILURE);
	}

	wl_display_set_global_filter(g_server.wl_display, server_global_filter,
		NULL);

	g_server.wl_event_loop = wl_display_get_event_loop(g_server.wl_display);

	/* Catch signals */
	g_server.sighup_source =
		wl_event_loop_add_signal(g_server.wl_event_loop, SIGHUP,
			handle_sighup, NULL);
	g_server.sigint_source =
		wl_event_loop_add_signal(g_server.wl_event_loop, SIGINT,
			handle_sigterm, g_server.wl_display);
	g_server.sigterm_source =
		wl_event_loop_add_signal(g_server.wl_event_loop, SIGTERM,
			handle_sigterm, g_server.wl_display);
	g_server.sigchld_source =
		wl_event_loop_add_signal(g_server.wl_event_loop, SIGCHLD,
			handle_sigchld, NULL);

	/*
	 * Prevent wayland clients that request the X11 clipboard but closing
	 * their read fd prematurely to crash labwc because of the unhandled
	 * SIGPIPE signal. It is caused by wlroots trying to write the X11
	 * clipboard data to the closed fd of the wayland client.
	 * See https://github.com/labwc/labwc/issues/890#issuecomment-1524962995
	 * for a reproducer involving xclip and wl-paste | head -c 1.
	 */
	signal(SIGPIPE, SIG_IGN);

	/*
	 * The backend is a feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an x11
	 * window if an x11 server is running.
	 */
	g_server.backend = wlr_backend_autocreate(g_server.wl_event_loop,
		&g_server.session);
	if (!g_server.backend) {
		wlr_log(WLR_ERROR, "unable to create backend");
		fprintf(stderr, helpful_seat_error_message);
		exit(EXIT_FAILURE);
	}

	/* Create headless backend to enable adding virtual outputs later on */
	wlr_multi_for_each_backend(g_server.backend, get_headless_backend,
		&g_server.headless.backend);

	if (!g_server.headless.backend) {
		wlr_log(WLR_DEBUG, "manually creating headless backend");
		g_server.headless.backend =
			wlr_headless_backend_create(g_server.wl_event_loop);
	} else {
		wlr_log(WLR_DEBUG, "headless backend already exists");
	}

	if (!g_server.headless.backend) {
		wlr_log(WLR_ERROR, "unable to create headless backend");
		exit(EXIT_FAILURE);
	}
	wlr_multi_backend_add(g_server.backend, g_server.headless.backend);

	/*
	 * If we don't populate headless backend with a virtual output (that we
	 * create and immediately destroy), then virtual outputs being added
	 * later do not work properly when overlaid on real output. Content is
	 * drawn on the virtual output, but not drawn on the real output.
	 */
	wlr_output_destroy(wlr_headless_add_output(g_server.headless.backend, 0,
		0));

	/*
	 * Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The
	 * user can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients.
	 */
	g_server.renderer = wlr_renderer_autocreate(g_server.backend);
	if (!g_server.renderer) {
		wlr_log(WLR_ERROR, "unable to create renderer");
		exit(EXIT_FAILURE);
	}

	g_server.renderer_lost.notify = handle_renderer_lost;
	wl_signal_add(&g_server.renderer->events.lost, &g_server.renderer_lost);

	if (!wlr_renderer_init_wl_shm(g_server.renderer, g_server.wl_display)) {
		wlr_log(WLR_ERROR, "Failed to initialize shared memory pool");
		exit(EXIT_FAILURE);
	}

	if (wlr_renderer_get_texture_formats(g_server.renderer,
			WLR_BUFFER_CAP_DMABUF)) {
		if (wlr_renderer_get_drm_fd(g_server.renderer) >= 0) {
			wlr_drm_create(g_server.wl_display, g_server.renderer);
		}
		g_server.linux_dmabuf =
			wlr_linux_dmabuf_v1_create_with_renderer(
				g_server.wl_display,
				LAB_WLR_LINUX_DMABUF_VERSION,
				g_server.renderer);
	} else {
		wlr_log(WLR_DEBUG, "unable to initialize dmabuf");
	}

	if (wlr_renderer_get_drm_fd(g_server.renderer) >= 0
			&& g_server.renderer->features.timeline
			&& g_server.backend->features.timeline) {
		wlr_linux_drm_syncobj_manager_v1_create(g_server.wl_display, 1,
			wlr_renderer_get_drm_fd(g_server.renderer));
	}

	/*
	 * Autocreates an allocator for us. The allocator is the bridge between
	 * the renderer and the backend. It handles the buffer creation,
	 * allowing wlroots to render onto the screen
	 */
	g_server.allocator =
		wlr_allocator_autocreate(g_server.backend, g_server.renderer);
	if (!g_server.allocator) {
		wlr_log(WLR_ERROR, "unable to create allocator");
		exit(EXIT_FAILURE);
	}

	wl_list_init(&g_server.views);
	wl_list_init(&g_server.unmanaged_surfaces);

	g_server.ssd_hover_state = ssd_hover_state_new();

	g_server.scene = wlr_scene_create();
	die_if_null(g_server.scene);

	g_server.direct_scanout_enabled =
		g_server.scene->WLR_PRIVATE.direct_scanout;

	/*
	 * The order in which the scene-trees below are created determines the
	 * z-order for nodes which cover the whole work-area.  For per-output
	 * scene-trees, see handle_new_output() in src/output.c
	 *
	 * | Type              | Scene Tree       | Per Output | Example
	 * | ----------------- | ---------------- | ---------- | -------
	 * | ext-session       | lock-screen      | Yes        | swaylock
	 * | osd               | osd_tree         | Yes        |
	 * | compositor-menu   | menu_tree        | No         | root-menu
	 * | layer-shell       | layer-popups     | Yes        |
	 * | layer-shell       | overlay-layer    | Yes        |
	 * | layer-shell       | top-layer        | Yes        | waybar
	 * | xwayland-OR       | unmanaged        | No         | dmenu
	 * | xdg-popups        | xdg-popups       | No         |
	 * | toplevels windows | always-on-top    | No         |
	 * | toplevels windows | normal           | No         | firefox
	 * | toplevels windows | always-on-bottom | No         | pcmanfm-qt --desktop
	 * | layer-shell       | bottom-layer     | Yes        | waybar
	 * | layer-shell       | background-layer | Yes        | swaybg
	 */

	g_server.view_tree_always_on_bottom =
		wlr_scene_tree_create(&g_server.scene->tree);
	g_server.view_tree = wlr_scene_tree_create(&g_server.scene->tree);
	g_server.view_tree_always_on_top =
		wlr_scene_tree_create(&g_server.scene->tree);
	g_server.xdg_popup_tree = wlr_scene_tree_create(&g_server.scene->tree);
#if HAVE_XWAYLAND
	g_server.unmanaged_tree = wlr_scene_tree_create(&g_server.scene->tree);
#endif
	g_server.menu_tree = wlr_scene_tree_create(&g_server.scene->tree);

	workspaces_init();

	output_init();

	/*
	 * Create some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device
	 * manager handles the clipboard. Each of these wlroots interfaces has
	 * room for you to dig your fingers in and play with their behavior if
	 * you want.
	 */
	g_server.compositor = wlr_compositor_create(g_server.wl_display,
		LAB_WLR_COMPOSITOR_VERSION, g_server.renderer);
	if (!g_server.compositor) {
		wlr_log(WLR_ERROR, "unable to create the wlroots compositor");
		exit(EXIT_FAILURE);
	}
	wlr_subcompositor_create(g_server.wl_display);

	struct wlr_data_device_manager *device_manager = NULL;
	device_manager = wlr_data_device_manager_create(g_server.wl_display);
	if (!device_manager) {
		wlr_log(WLR_ERROR, "unable to create data device manager");
		exit(EXIT_FAILURE);
	}

	/*
	 * Empirically, primary selection doesn't work with Gtk apps unless the
	 * device manager is one of the earliest globals to be advertised. All
	 * credit to Wayfire for discovering this, though their symptoms
	 * (crash) are not the same as ours (silently does nothing). When adding
	 * more globals above this line it would be as well to check that
	 * middle-button paste still works with any Gtk app of your choice
	 *
	 * https://wayfire.org/2020/08/04/Wayfire-0-5.html
	 */
	if (rc.primary_selection) {
		wlr_primary_selection_v1_device_manager_create(
			g_server.wl_display);
	}

	g_server.input_method_manager =
		wlr_input_method_manager_v2_create(g_server.wl_display);
	g_server.text_input_manager =
		wlr_text_input_manager_v3_create(g_server.wl_display);
	seat_init();
	xdg_shell_init();
	kde_server_decoration_init();
	xdg_server_decoration_init();

	struct wlr_presentation *presentation =
		wlr_presentation_create(g_server.wl_display, g_server.backend,
			LAB_WLR_PRESENTATION_TIME_VERSION);
	if (!presentation) {
		wlr_log(WLR_ERROR, "unable to create presentation interface");
		exit(EXIT_FAILURE);
	}
	if (g_server.linux_dmabuf) {
		wlr_scene_set_linux_dmabuf_v1(g_server.scene,
			g_server.linux_dmabuf);
	}

	wlr_export_dmabuf_manager_v1_create(g_server.wl_display);
	wlr_screencopy_manager_v1_create(g_server.wl_display);
	wlr_ext_image_copy_capture_manager_v1_create(g_server.wl_display, 1);
	wlr_ext_output_image_capture_source_manager_v1_create(
		g_server.wl_display, 1);
	wlr_data_control_manager_v1_create(g_server.wl_display);
	wlr_ext_data_control_manager_v1_create(g_server.wl_display,
		LAB_EXT_DATA_CONTROL_VERSION);
	g_server.security_context_manager_v1 =
		wlr_security_context_manager_v1_create(g_server.wl_display);
	wlr_viewporter_create(g_server.wl_display);
	wlr_single_pixel_buffer_manager_v1_create(g_server.wl_display);
	wlr_fractional_scale_manager_v1_create(g_server.wl_display,
		LAB_WLR_FRACTIONAL_SCALE_V1_VERSION);

	idle_manager_create(g_server.wl_display);

	g_server.relative_pointer_manager =
		wlr_relative_pointer_manager_v1_create(g_server.wl_display);
	g_server.constraints =
		wlr_pointer_constraints_v1_create(g_server.wl_display);

	g_server.new_constraint.notify = create_constraint;
	wl_signal_add(&g_server.constraints->events.new_constraint,
		&g_server.new_constraint);

	g_server.foreign_toplevel_manager =
		wlr_foreign_toplevel_manager_v1_create(g_server.wl_display);

	g_server.foreign_toplevel_list =
		wlr_ext_foreign_toplevel_list_v1_create(g_server.wl_display,
			LAB_EXT_FOREIGN_TOPLEVEL_LIST_VERSION);

	wlr_alpha_modifier_v1_create(g_server.wl_display);

	session_lock_init();

	g_server.drm_lease_manager =
		wlr_drm_lease_v1_manager_create(g_server.wl_display,
			g_server.backend);
	if (g_server.drm_lease_manager) {
		g_server.drm_lease_request.notify = handle_drm_lease_request;
		wl_signal_add(&g_server.drm_lease_manager->events.request,
			&g_server.drm_lease_request);
	} else {
		wlr_log(WLR_DEBUG, "Failed to create wlr_drm_lease_device_v1");
		wlr_log(WLR_INFO, "VR will not be available");
	}

	g_server.output_power_manager_v1 =
		wlr_output_power_manager_v1_create(g_server.wl_display);
	g_server.output_power_manager_set_mode.notify =
		handle_output_power_manager_set_mode;
	wl_signal_add(&g_server.output_power_manager_v1->events.set_mode,
		&g_server.output_power_manager_set_mode);

	g_server.tearing_control =
		wlr_tearing_control_manager_v1_create(g_server.wl_display, 1);
	g_server.tearing_new_object.notify = handle_tearing_new_object;
	wl_signal_add(&g_server.tearing_control->events.new_object,
		&g_server.tearing_new_object);

	g_server.tablet_manager = wlr_tablet_v2_create(g_server.wl_display);

	layers_init();

	/* These get cleaned up automatically on display destroy */
	struct wlr_xdg_foreign_registry *registry =
		wlr_xdg_foreign_registry_create(g_server.wl_display);
	wlr_xdg_foreign_v1_create(g_server.wl_display, registry);
	wlr_xdg_foreign_v2_create(g_server.wl_display, registry);

#if HAVE_LIBSFDO
	desktop_entry_init();
#endif

#if HAVE_XWAYLAND
	xwayland_server_init(g_server.compositor);
#endif
}

void
server_start(void)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(g_server.wl_display);
	if (!socket) {
		wlr_log_errno(WLR_ERROR, "unable to open wayland socket");
		exit(EXIT_FAILURE);
	}

	/*
	 * Start the backend. This will enumerate outputs and inputs, become
	 * the DRM master, etc
	 */
	if (!wlr_backend_start(g_server.backend)) {
		wlr_log(WLR_ERROR, "unable to start the wlroots backend");
		exit(EXIT_FAILURE);
	}

	/* Potentially set up the initial fallback output */
	output_virtual_update_fallback();

	if (setenv("WAYLAND_DISPLAY", socket, true) < 0) {
		wlr_log_errno(WLR_ERROR, "unable to set WAYLAND_DISPLAY");
	} else {
		wlr_log(WLR_DEBUG, "WAYLAND_DISPLAY=%s", socket);
	}
}

void
server_finish(void)
{
#if HAVE_XWAYLAND
	xwayland_server_finish();
#endif
#if HAVE_LIBSFDO
	desktop_entry_finish();
#endif
	wl_event_source_remove(g_server.sighup_source);
	wl_event_source_remove(g_server.sigint_source);
	wl_event_source_remove(g_server.sigterm_source);
	wl_event_source_remove(g_server.sigchld_source);

	wl_display_destroy_clients(g_server.wl_display);

	seat_finish();
	output_finish();
	xdg_shell_finish();
	layers_finish();
	kde_server_decoration_finish();
	xdg_server_decoration_finish();
	wl_list_remove(&g_server.new_constraint.link);
	wl_list_remove(&g_server.output_power_manager_set_mode.link);
	wl_list_remove(&g_server.tearing_new_object.link);
	if (g_server.drm_lease_request.notify) {
		wl_list_remove(&g_server.drm_lease_request.link);
		g_server.drm_lease_request.notify = NULL;
	}

	wlr_backend_destroy(g_server.backend);
	wlr_allocator_destroy(g_server.allocator);

	wl_list_remove(&g_server.renderer_lost.link);
	wlr_renderer_destroy(g_server.renderer);

	workspaces_destroy();
	wlr_scene_node_destroy(&g_server.scene->tree.node);

	wl_display_destroy(g_server.wl_display);
	free(g_server.ssd_hover_state);
}
