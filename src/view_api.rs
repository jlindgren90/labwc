// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::lazy_static;
use crate::util::*;
use crate::view::*;
use crate::view_geom::*;
use crate::views::*;
use std::ffi::c_char;
use std::ptr::null;

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
pub extern "C" fn view_get_root(id: ViewId) -> *mut CView {
    let views = views();
    return views.get_c_ptr(views.get_root_of(id));
}

#[unsafe(no_mangle)]
pub extern "C" fn view_get_size_hints(id: ViewId) -> ViewSizeHints {
    let views = views();
    let view = views.get_view(id);
    return view.map_or(ViewSizeHints::default(), View::get_size_hints);
}

#[unsafe(no_mangle)]
pub extern "C" fn view_has_strut_partial(id: ViewId) -> bool {
    let views = views();
    let view = views.get_view(id);
    return view.map_or(false, View::has_strut_partial);
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
pub extern "C" fn view_set_ssd_enabled(id: ViewId, ssd_enabled: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_ssd_enabled(ssd_enabled);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_maximized(id: ViewId, maximized: ViewAxis) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_maximized(maximized);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_tiled(id: ViewId, tiled: LabEdge) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_tiled(tiled);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_compute_default_geom(
    view_st: &ViewState,
    geom: &mut Rect,
    rel_to: Option<&Rect>,
) {
    compute_default_geom(
        view_st,
        geom,
        *rel_to.unwrap_or(&Rect::default()),
        /* keep_position */ false,
    );
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_current_pos(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_current_pos(x, y);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_current_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_current_size(width, height);
    }
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
pub extern "C" fn view_adjust_initial_geom(id: ViewId, keep_position: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.adjust_initial_geom(keep_position);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_store_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.store_natural_geom();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_apply_special_geom(id: ViewId) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.apply_special_geom();
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
    views_mut().adjust_for_layout_change();
}

#[unsafe(no_mangle)]
pub extern "C" fn view_fullscreen(id: ViewId, fullscreen: bool) {
    let view_ptr = views_mut().fullscreen(id, fullscreen);
    if !view_ptr.is_null() {
        unsafe { view_notify_fullscreen(view_ptr) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_maximize(id: ViewId, axis: ViewAxis) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.maximize(axis);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_tile(id: ViewId, edge: LabEdge) {
    if let Some(view) = views_mut().get_view_mut(id) {
        if edge != LAB_EDGE_NONE {
            // Unmaximize, otherwise tiling will have no effect
            view.maximize(VIEW_AXIS_NONE);
        }
        view.tile(edge);
    }
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
pub extern "C" fn view_refocus_active() {
    views().refocus_active();
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
