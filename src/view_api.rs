// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::lazy_static;
use crate::util::*;
use crate::views::*;
use std::ffi::c_char;
use std::ptr::{null, null_mut};

#[unsafe(no_mangle)]
pub extern "C" fn view_is_focusable(state: &ViewState) -> bool {
    state.focusable()
}

#[unsafe(no_mangle)]
pub extern "C" fn view_is_floating(state: &ViewState) -> bool {
    state.floating()
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[unsafe(no_mangle)]
pub extern "C" fn view_add(c_ptr: *mut CView, is_xwayland: bool) -> ViewId {
    views_mut().add(c_ptr, is_xwayland)
}

#[unsafe(no_mangle)]
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().remove(id);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_count() -> usize {
    views().count()
}

#[unsafe(no_mangle)]
pub extern "C" fn view_nth(n: usize) -> *mut CView {
    views().get_nth(n)
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_state(id: ViewId) -> *const ViewState {
    views().get_view(id).map_or(null(), |v| v.get_state())
}

#[unsafe(no_mangle)]
pub extern "C" fn view_adjust_size(id: ViewId, width: &mut i32, height: &mut i32) {
    if let Some(view) = views().get_view(id) {
        view.adjust_size(width, height);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_app_id(id: ViewId, app_id: *const c_char) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_app_id(cstring(app_id));
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_title(id: ViewId, title: *const c_char) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_title(cstring(title));
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_map_common(id: ViewId, focus_mode: ViewFocusMode) {
    views_mut().map_common(id, focus_mode);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_unmap_common(id: ViewId) {
    views_mut().unmap_common(id);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_active() -> *mut CView {
    views().get_active()
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_active(id: ViewId) {
    views_mut().set_active(id);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_pending_geom(id: ViewId, geom: Rect) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_pending_geom(geom);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_move_resize(id: ViewId, geom: Rect) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.move_resize(geom);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_commit_move(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.commit_move(x, y);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_commit_geom(id: ViewId, width: i32, height: i32) {
    views_mut().commit_geom(id, width, height);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_initial_geom(id: ViewId, rel_to: Option<&Rect>, keep_position: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_initial_geom(rel_to, keep_position);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_late_client_size(
    id: ViewId,
    width: i32,
    height: i32,
    cursor_x: i32,
    cursor_y: i32,
) {
    views_mut().set_late_client_size(id, width, height, cursor_x, cursor_y);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_output(id: ViewId, output: *mut Output) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_output(output);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn views_adjust_for_layout_change() {
    views_mut().adjust_for_layout_change();
}

#[unsafe(no_mangle)]
pub extern "C" fn view_enable_ssd(id: ViewId, enabled: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.enable_ssd(enabled);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_destroy_ssd(id: ViewId) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.destroy_ssd();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_fullscreen(id: ViewId, fullscreen: bool) {
    views_mut().fullscreen(id, fullscreen);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_maximize(id: ViewId, axis: ViewAxis) {
    views_mut().maximize(id, axis);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_tile(id: ViewId, edge: LabEdge) {
    let mut views = views_mut();
    if edge != LAB_EDGE_NONE {
        // Unmaximize, otherwise tiling will have no effect
        views.maximize(id, VIEW_AXIS_NONE);
    }
    views.tile(id, edge);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_minimize(id: ViewId, minimized: bool) {
    views_mut().minimize(id, minimized);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_raise(id: ViewId) {
    views_mut().raise(id);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_focus(id: ViewId, raise: bool) {
    let mut views = views_mut();
    // Unminimizing also focuses (and raises) the view
    if !views.minimize(id, false) {
        views.focus(id, raise);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_focus_topmost() {
    views_mut().focus_topmost();
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_inhibits_keybinds(id: ViewId, inhibits_keybinds: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_inhibits_keybinds(inhibits_keybinds);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_close(id: ViewId) {
    if let Some(view) = views().get_view(id) {
        view.close();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn views_add_foreign_toplevel_client(client: *mut WlResource) {
    views_mut().add_foreign_toplevel_client(client);
}

#[unsafe(no_mangle)]
pub extern "C" fn views_remove_foreign_toplevel_client(client: *mut WlResource) {
    views_mut().remove_foreign_toplevel_client(client);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_remove_foreign_toplevel(id: ViewId, resource: *mut WlResource) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.remove_foreign_toplevel(resource);
    }
}

// Transfers ownership of the surface to the view
#[unsafe(no_mangle)]
pub extern "C" fn view_add_icon_surface(id: ViewId, surface: *mut CairoSurface) {
    let surf = CairoSurfacePtr::new(surface);
    if let Some(view) = views_mut().get_view_mut(id) {
        view.add_icon_surface(surf);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_clear_icon_surfaces(id: ViewId) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.clear_icon_surfaces();
    }
}

// Does NOT transfer ownership out of the view
#[unsafe(no_mangle)]
pub extern "C" fn view_get_icon_buffer(id: ViewId) -> *mut WlrBuffer {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.get_icon_buffer()
    } else {
        null_mut()
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_update_icon(id: ViewId) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.update_icon();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_reload_ssds() {
    views_mut().reload_ssds();
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_grab_context(id: ViewId, cursor_x: i32, cursor_y: i32, edges: LabEdge) {
    views_mut().set_grab_context(id, cursor_x, cursor_y, edges);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_start_move(id: ViewId) -> bool {
    views_mut().start_move(id)
}

#[unsafe(no_mangle)]
pub extern "C" fn view_continue_move(cursor_x: i32, cursor_y: i32) {
    views_mut().continue_move(cursor_x, cursor_y);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_start_resize(id: ViewId, edges: LabEdge) -> bool {
    views_mut().start_resize(id, edges)
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_resizing() -> *mut CView {
    views().get_resizing()
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_resize_edges() -> LabEdge {
    views().get_resize_edges()
}

#[unsafe(no_mangle)]
pub extern "C" fn view_continue_resize(cursor_x: i32, cursor_y: i32) {
    views_mut().continue_resize(cursor_x, cursor_y);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_finish_grab(cursor_x: i32, cursor_y: i32) {
    let mut views = views_mut();
    views.snap_to_edge(cursor_x, cursor_y);
    views.reset_grab_for(None);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_reset_grab() {
    views_mut().reset_grab_for(None);
}

#[unsafe(no_mangle)]
pub extern "C" fn cycle_list_build() {
    views_mut().build_cycle_list();
}

#[unsafe(no_mangle)]
pub extern "C" fn cycle_list_len() -> usize {
    views().cycle_list_len()
}

#[unsafe(no_mangle)]
pub extern "C" fn cycle_list_nth(n: usize) -> *mut CView {
    views().cycle_list_nth(n)
}
