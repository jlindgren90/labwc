// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::lazy_static;
use crate::util::*;
use crate::view::*;
use crate::view_geom::*;
use crate::views::*;
use std::ffi::c_char;
use std::ptr::{null, null_mut};

#[unsafe(no_mangle)]
pub extern "C" fn view_is_floating(state: &ViewState) -> bool {
    state.floating()
}

fn do_update(level: UpdateLevel) {
    if level >= UpdateLevel::Cursor {
        unsafe { cursor_update_focus() };
    }
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
        let hints = view.get_size_hints();
        adjust_size_for_hints(&hints, width, height);
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
pub extern "C" fn view_map_common(id: ViewId) {
    let ul = views_mut().map_common(id);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_unmap_common(id: ViewId) {
    let ul = views_mut().unmap_common(id);
    do_update(ul);
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
pub extern "C" fn view_compute_default_geom(
    view_st: &ViewState,
    geom: &mut Rect,
    rel_to: Option<&Rect>,
    keep_position: bool,
) {
    compute_default_geom(
        view_st,
        geom,
        *rel_to.unwrap_or(&Rect::default()),
        keep_position,
    );
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_pending_geom(id: ViewId, geom: Rect) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_pending_geom(geom);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_move_resize(id: ViewId, geom: Rect) {
    let ul = if let Some(view) = views_mut().get_view_mut(id) {
        view.move_resize(geom)
    } else {
        return;
    };
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_commit_move(id: ViewId, x: i32, y: i32) {
    let ul = if let Some(view) = views_mut().get_view_mut(id) {
        view.commit_move(x, y)
    } else {
        return;
    };
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_commit_geom(id: ViewId, width: i32, height: i32) {
    let ul = views_mut().commit_geom(id, width, height);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_fallback_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_fallback_natural_geom();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_output(id: ViewId, output: *mut Output) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_output(output);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn views_adjust_for_layout_change() {
    let ul = views_mut().adjust_for_layout_change();
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_enable_ssd(id: ViewId, enabled: bool) {
    let ul = if let Some(view) = views_mut().get_view_mut(id) {
        view.enable_ssd(enabled)
    } else {
        return;
    };
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_destroy_ssd(id: ViewId) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.destroy_ssd();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_fullscreen(id: ViewId, fullscreen: bool) {
    let ul = views_mut().fullscreen(id, fullscreen);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_maximize(id: ViewId, axis: ViewAxis) {
    let ul = views_mut().maximize(id, axis);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_tile(id: ViewId, edge: LabEdge) {
    let mut ul = UpdateLevel::None;
    if edge != LAB_EDGE_NONE {
        // Unmaximize, otherwise tiling will have no effect
        ul |= views_mut().maximize(id, VIEW_AXIS_NONE);
    }
    ul |= views_mut().tile(id, edge);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_minimize(id: ViewId, minimized: bool) {
    let ul = views_mut().minimize(id, minimized).1;
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_raise(id: ViewId) {
    let ul = views_mut().raise(id);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_focus(id: ViewId, raise: bool) {
    // Unminimizing also focuses (and raises) the view
    let (was_shown, mut ul) = views_mut().minimize(id, false);
    if !was_shown {
        ul |= views_mut().focus(id, raise);
    }
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_focus_topmost() {
    let ul = views_mut().focus_topmost();
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_refocus_active() {
    views().refocus_active();
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
    let ul = views_mut().reload_ssds();
    do_update(ul);
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
pub extern "C" fn view_get_moving() -> *mut CView {
    views().get_moving()
}

#[unsafe(no_mangle)]
pub extern "C" fn view_adjust_move_origin(width: i32, height: i32) {
    views_mut().adjust_move_origin(width, height);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_compute_move_position(
    cursor_x: i32,
    cursor_y: i32,
    x: &mut i32,
    y: &mut i32,
) {
    (*x, *y) = views().compute_move_position(cursor_x, cursor_y);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_continue_move(cursor_x: i32, cursor_y: i32) {
    let ul = views_mut().continue_move(cursor_x, cursor_y);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_start_resize(id: ViewId, edges: LabEdge) -> bool {
    views_mut().start_resize(id, edges)
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_resize_edges() -> LabEdge {
    views().get_resize_edges()
}

#[unsafe(no_mangle)]
pub extern "C" fn view_continue_resize(cursor_x: i32, cursor_y: i32) {
    let ul = views_mut().continue_resize(cursor_x, cursor_y);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_finish_grab(cursor_x: i32, cursor_y: i32) {
    let mut ul = views_mut().snap_to_edge(cursor_x, cursor_y);
    ul |= views_mut().reset_grab_for(None);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_reset_grab() {
    let ul = views_mut().reset_grab_for(None);
    do_update(ul);
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
pub extern "C" fn cycle_list_nth(n: usize) -> ViewId {
    views().cycle_list_nth(n)
}
