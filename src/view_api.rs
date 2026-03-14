// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::lazy_static;
use crate::util::*;
use crate::view::*;
use crate::views::*;
use std::ffi::c_char;
use std::ptr::{null, null_mut};

#[unsafe(no_mangle)]
pub extern "C" fn view_is_floating(state: &ViewState) -> bool {
    state.floating()
}

fn do_update(level: UpdateLevel) {
    if level >= UpdateLevel::UsableArea {
        unsafe {
            output_update_all_usable_areas(/* layout_changed */ false)
        };
    }
    if level >= UpdateLevel::Cursor {
        unsafe { cursor_update_focus() };
    }
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[unsafe(no_mangle)]
pub extern "C" fn view_add_xdg(c_ptr: *mut CView) -> ViewId {
    views_mut().add(ViewSpec::Xdg(c_ptr))
}

#[unsafe(no_mangle)]
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().remove(id);
    unsafe { menu_on_view_destroy(id) };
    unsafe { cursor_update_focus() };
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_state(id: ViewId) -> *const ViewState {
    views().get_view(id).map_or(null(), |v| v.get_state())
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_scene(id: ViewId) -> *const ViewScene {
    views().get_view(id).map_or(null(), |v| v.get_scene())
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
pub extern "C" fn view_set_strut_partial(id: ViewId, strut_partial: Option<&ViewStrutPartial>) {
    let ul = if let Some(view) = views_mut().get_view_mut(id) {
        view.set_strut_partial(strut_partial)
    } else {
        return;
    };
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_map(id: ViewId) {
    let ul = views_mut().map(id);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_unmap(id: ViewId) {
    let ul = views_mut().unmap(id);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_always_on_top(id: ViewId, always_on_top: bool) {
    let ul = views_mut().set_always_on_top(id, always_on_top);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_toggle_always_on_top(id: ViewId) {
    let always_on_top;
    if let Some(view) = views().get_view(id) {
        always_on_top = view.get_state().always_on_top;
    } else {
        return;
    }
    view_set_always_on_top(id, !always_on_top);
}

#[unsafe(no_mangle)]
pub extern "C" fn views_adjust_usable_area(output: *mut Output) {
    views().adjust_usable_area(output);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_active() -> ViewId {
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
pub extern "C" fn view_set_initial_commit_size(
    id: ViewId,
    width: i32,
    height: i32,
    cursor_x: i32,
    cursor_y: i32,
) {
    views_mut().set_initial_commit_size(id, width, height, cursor_x, cursor_y);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_commit_resize_timeout(id: ViewId) {
    let ul = if let Some(view) = views_mut().get_view_mut(id) {
        view.commit_resize_timeout()
    } else {
        return;
    };
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn views_on_output_destroy(output: *mut Output) {
    views_mut().on_output_destroy(output);
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
pub extern "C" fn view_fullscreen(id: ViewId, fullscreen: bool, output: *mut Output) {
    let ul = views_mut().fullscreen(id, fullscreen, output);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_toggle_fullscreen(id: ViewId) {
    let fullscreen;
    if let Some(view) = views().get_view(id) {
        fullscreen = view.get_state().fullscreen;
    } else {
        return;
    }
    view_fullscreen(id, !fullscreen, /* output */ null_mut());
}

#[unsafe(no_mangle)]
pub extern "C" fn view_maximize(id: ViewId, axis: ViewAxis) {
    let ul = views_mut().maximize(id, axis);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_toggle_maximize(id: ViewId, axis: ViewAxis) {
    let maximized;
    if let Some(view) = views().get_view(id) {
        maximized = view.get_state().maximized;
    } else {
        return;
    }
    if axis == VIEW_AXIS_HORIZONTAL || axis == VIEW_AXIS_VERTICAL {
        view_maximize(id, maximized ^ axis);
    } else if axis == VIEW_AXIS_BOTH {
        if maximized == VIEW_AXIS_BOTH {
            view_maximize(id, VIEW_AXIS_NONE);
        } else {
            view_maximize(id, VIEW_AXIS_BOTH);
        }
    }
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
    let ul = views_mut().raise(id, /* force_restack */ false);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_focus(id: ViewId, raise: bool) {
    // Unminimizing also focuses (and raises) the view
    let (was_shown, mut ul) = views_mut().minimize(id, false);
    if !was_shown {
        ul |= views_mut().focus(id, raise, /* force_restack */ false);
    }
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_focus_topmost() {
    let ul = views_mut().focus_topmost();
    do_update(ul);
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

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_add(xid: XId, xsurface: *mut XSurface) -> *const XSurfaceInfo {
    views_mut().add_xsurface(xid, xsurface)
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_lookup(xid: XId) -> *mut XSurface {
    views()
        .get_xwm()
        .get_info(xid)
        .map_or(null_mut(), |s| s.xsurface)
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_get_for_serial(serial: u64) -> *mut XSurface {
    views().get_xwm().get_for_serial(serial)
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_get_for_surface_id(surface_id: u32) -> *mut XSurface {
    views().get_xwm().get_for_surface_id(surface_id)
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_set_managed(xid: XId, managed: bool) {
    views_mut().set_xsurface_managed(xid, managed);
}

// Called from xwayland_surface_destroy()
#[unsafe(no_mangle)]
pub extern "C" fn xsurface_on_destroy(xid: XId) {
    views_mut().remove_xsurface(xid);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_destroy_all() {
    let xsurfaces = views().get_xwm().get_all();
    for xsurface in xsurfaces {
        unsafe { xwayland_surface_destroy(xsurface) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_set_parent_xid(xid: XId, parent_xid: XId) {
    views_mut().set_parent_xid(xid, parent_xid);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_set_serial(xid: XId, serial: u64) {
    views_mut().set_xsurface_serial(xid, serial);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_set_surface_id(xid: XId, surface_id: u32) {
    views_mut().set_xsurface_surface_id(xid, surface_id);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_map(xid: XId, surface: *mut WlrSurface) {
    let ul = views_mut().map_xsurface(xid, surface);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_unmap(xid: XId, surface: *mut WlrSurface) {
    let ul = views_mut().unmap_xsurface(xid, surface);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_request_configure(xid: XId, geom: Rect) {
    let ul = views_mut().request_xsurface_configure(xid, geom);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_move_unmanaged(xid: XId, x: i32, y: i32) {
    let ul = views().move_unmanaged(xid, x, y);
    do_update(ul);
}

#[unsafe(no_mangle)]
pub extern "C" fn xsurface_focus(xid: XId, surface: *mut WlrSurface, raise: bool) {
    let ul = views_mut().focus_xsurface(xid, surface, raise);
    do_update(ul);
}
