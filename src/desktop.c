// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>
#include "common/scene-helpers.h"
#include "dnd.h"
#include "labwc.h"
#include "layers.h"
#include "node.h"
#include "output.h"
#include "ssd.h"
#include "view.h"

void
desktop_arrange_all_views(void)
{
	/*
	 * Adjust window positions/sizes. Skip views with no size since
	 * we can't do anything useful with them; they will presumably
	 * be initialized with valid positions/sizes later.
	 *
	 * We do not simply check view->mapped/been_mapped here because
	 * views can have maximized/fullscreen geometry applied while
	 * still unmapped. We do want to adjust the geometry of those
	 * views.
	 */
	struct view *view;
	wl_list_for_each(view, &g_server.views, link) {
		if (!wlr_box_empty(&view->pending)) {
			view_adjust_for_layout_change(view);
		}
	}
	/* Update top layer visibility after any/all views have moved */
	desktop_update_top_layer_visibility();
}

static void
set_or_offer_focus(struct view *view)
{
	struct wlr_surface *surface = view_get_surface(view);

	switch (view->st->focus_mode) {
	case VIEW_FOCUS_MODE_ALWAYS:
		if (surface != g_seat.wlr_seat->keyboard_state.focused_surface) {
			seat_focus_surface(surface);
		}
		break;
	case VIEW_FOCUS_MODE_LIKELY:
	case VIEW_FOCUS_MODE_UNLIKELY:
		if (surface != g_seat.wlr_seat->keyboard_state.focused_surface) {
			view_offer_focus(view->id);
		}
		break;
	case VIEW_FOCUS_MODE_NEVER:
		break;
	}
}

void
desktop_focus_view(struct view *view, bool raise)
{
	assert(view);
	/*
	 * Guard against views with no mapped surfaces when handling
	 * 'request_activate' and 'request_minimize'.
	 */
	if (!view->st->mapped) {
		return;
	}

	if (g_server.input_mode == LAB_INPUT_STATE_CYCLE) {
		wlr_log(WLR_DEBUG, "not focusing window while window switching");
		return;
	}

	if (view->st->minimized) {
		/*
		 * Unminimizing will map the view which triggers a call to this
		 * function again (with raise=true).
		 */
		view_minimize(view, false);
		return;
	}

	if (!view->st->mapped) {
		return;
	}

	if (raise) {
		view_move_to_front(view);
	}

	/*
	 * If any child/sibling of the view is a modal dialog, focus
	 * the dialog instead. It does not need to be raised separately
	 * since view_move_to_front() raises all sibling views together.
	 */
	struct view *dialog = view_get_modal_dialog(view);
	set_or_offer_focus(dialog ? dialog : view);
}

/* TODO: focus layer-shell surfaces also? */
void
desktop_focus_view_or_surface(struct view *view,
		struct wlr_surface *surface, bool raise)
{
	assert(view || surface);
	if (view) {
		desktop_focus_view(view, raise);
	} else {
		struct wlr_xwayland_surface *xsurface =
			wlr_xwayland_surface_try_from_wlr_surface(surface);
		if (xsurface && wlr_xwayland_surface_override_redirect_wants_focus(xsurface)) {
			seat_focus_surface(surface);
		}
	}
}

static struct view *
desktop_topmost_focusable_view(void)
{
	struct view *view;
	wl_list_for_each(view, &g_server.views, link) {
		if (view_is_focusable(view->st) && !view->st->minimized) {
			return view;
		}
	}
	return NULL;
}

void
desktop_focus_topmost_view(void)
{
	struct view *view = desktop_topmost_focusable_view();
	if (view) {
		desktop_focus_view(view, /*raise*/ true);
	} else {
		/*
		 * Defocus previous focused surface/view if no longer
		 * focusable (e.g. unmapped or on a different workspace).
		 */
		seat_focus_surface(NULL);
	}
}

void
desktop_update_top_layer_visibility(void)
{
	struct view *view;
	struct output *output;
	uint32_t top = ZWLR_LAYER_SHELL_V1_LAYER_TOP;

	/* Enable all top layers */
	wl_list_for_each(output, &g_server.outputs, link) {
		if (!output_is_usable(output)) {
			continue;
		}
		wlr_scene_node_set_enabled(&output->layer_tree[top]->node, true);
	}

	/*
	 * And disable them again when there is a fullscreen view without
	 * any views above it
	 */
	uint64_t outputs_covered = 0;
	wl_list_for_each(view, &g_server.views, link) {
		if (!view->st->mapped || view->st->minimized) {
			continue;
		}
		if (!output_is_usable(view->output)) {
			continue;
		}
		if (view->st->fullscreen && !(view->output->id_bit & outputs_covered)) {
			wlr_scene_node_set_enabled(
				&view->output->layer_tree[top]->node, false);
		}
		outputs_covered |= view->output->id_bit;
	}
}

/*
 * Work around rounding issues in some clients (notably Qt apps) where
 * cursor coordinates in the rightmost or bottom pixel are incorrectly
 * rounded up, putting them outside the surface bounds. The effect is
 * especially noticeable in right/bottom desktop panels, since driving
 * the cursor to the edge of the screen no longer works.
 *
 * Under X11, such rounding issues went unnoticed since cursor positions
 * were always integers (i.e. whole pixel boundaries) anyway. Until more
 * clients/toolkits are fractional-pixel clean, limit surface cursor
 * coordinates to (w - 1, h - 1) as a workaround.
 */
static void
avoid_edge_rounding_issues(struct cursor_context *ctx)
{
	if (!ctx->surface) {
		return;
	}

	int w = ctx->surface->current.width;
	int h = ctx->surface->current.height;
	/*
	 * The cursor isn't expected to be outside the surface bounds
	 * here, but check (sx < w, sy < h) just in case.
	 */
	if (ctx->sx > w - 1 && ctx->sx < w) {
		ctx->sx = w - 1;
	}
	if (ctx->sy > h - 1 && ctx->sy < h) {
		ctx->sy = h - 1;
	}
}

/* TODO: make this less big and scary */
struct cursor_context
get_cursor_context(void)
{
	struct cursor_context ret = {.type = LAB_NODE_NONE};
	struct wlr_cursor *cursor = g_seat.cursor;

	/* Prevent drag icons to be on top of the hitbox detection */
	if (g_seat.drag.active) {
		dnd_icons_show(false);
	}

	struct wlr_scene_node *node =
		wlr_scene_node_at(&g_server.scene->tree.node,
			cursor->x, cursor->y, &ret.sx, &ret.sy);

	if (g_seat.drag.active) {
		dnd_icons_show(true);
	}

	if (!node) {
		ret.type = LAB_NODE_ROOT;
		return ret;
	}
	ret.node = node;
	ret.surface = lab_wlr_surface_from_node(node);

	avoid_edge_rounding_issues(&ret);

	/* TODO: attach LAB_NODE_UNMANAGED node-descriptor to unmanaged surfaces */
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		if (node->parent == g_server.unmanaged_tree) {
			ret.type = LAB_NODE_UNMANAGED;
			return ret;
		}
	}

	while (node) {
		struct node_descriptor *desc = node->data;
		if (desc) {
			switch (desc->type) {
			case LAB_NODE_VIEW:
			case LAB_NODE_XDG_POPUP:
				ret.view = desc->view;
				if (ret.surface) {
					ret.type = LAB_NODE_CLIENT;
				} else {
					/* e.g. when cursor is on resize-indicator */
					ret.type = LAB_NODE_NONE;
				}
				return ret;
			case LAB_NODE_LAYER_SURFACE:
				ret.type = LAB_NODE_LAYER_SURFACE;
				return ret;
			case LAB_NODE_LAYER_POPUP:
			case LAB_NODE_SESSION_LOCK_SURFACE:
			case LAB_NODE_IME_POPUP:
				ret.type = LAB_NODE_CLIENT;
				return ret;
			case LAB_NODE_MENUITEM:
				/* Always return the top scene node for menu items */
				ret.node = node;
				ret.type = LAB_NODE_MENUITEM;
				return ret;
			case LAB_NODE_CYCLE_OSD_ITEM:
				/* Always return the top scene node for osd items */
				ret.node = node;
				ret.type = LAB_NODE_CYCLE_OSD_ITEM;
				return ret;
			case LAB_NODE_BUTTON_FIRST...LAB_NODE_BUTTON_LAST:
			case LAB_NODE_SSD_ROOT:
			case LAB_NODE_TITLE:
			case LAB_NODE_TITLEBAR:
				/* Always return the top scene node for ssd parts */
				ret.node = node;
				ret.view = desc->view;
				/*
				 * A node_descriptor attached to a ssd part
				 * must have an associated view.
				 */
				assert(ret.view);

				/*
				 * When cursor is on the ssd border or extents,
				 * desc->type is usually LAB_NODE_SSD_ROOT.
				 * But desc->type can also be LAB_NODE_TITLEBAR
				 * when cursor is on the curved border at the
				 * titlebar.
				 *
				 * ssd_get_resizing_type() overwrites both of
				 * them with LAB_NODE_{BORDER,CORNER}_* node
				 * types, which are mapped to mouse contexts
				 * like Left and TLCorner.
				 */
				ret.type = ssd_get_resizing_type(ret.view->ssd, cursor);
				if (ret.type == LAB_NODE_NONE) {
					/*
					 * If cursor is not on border/extents,
					 * just use desc->type which should be
					 * mapped to mouse contexts like Title,
					 * Titlebar and Iconify.
					 */
					ret.type = desc->type;
				}

				return ret;
			default:
				/* Other node types are not attached a scene node */
				wlr_log(WLR_ERROR, "unexpected node type: %d", desc->type);
				break;
			}
		}

		/* node->parent is always a *wlr_scene_tree */
		node = node->parent ? &node->parent->node : NULL;
	}

	/*
	 * TODO: add node descriptors for the OSDs and reinstate
	 *       wlr_log(WLR_DEBUG, "Unknown node detected");
	 */
	return ret;
}

