// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::lazy_static;
use crate::util::*;
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
pub extern "C" fn view_get_state(id: ViewId) -> *const ViewState {
    views().get_view(id).map_or(null(), |v| v.get_state())
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
    let view_ptr = views_mut().map_common(id, focus_mode);
    if !view_ptr.is_null() {
        unsafe { view_notify_map(view_ptr) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_unmap_common(id: ViewId) {
    let view_ptr = views_mut().unmap_common(id);
    if !view_ptr.is_null() {
        unsafe { view_notify_unmap(view_ptr) };
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_active(id: ViewId, active: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_active(active);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_fullscreen_internal(id: ViewId, fullscreen: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_fullscreen(fullscreen);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_maximized(id: ViewId, maximized: ViewAxis) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_maximized(maximized);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_minimized(id: ViewId, minimized: bool) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_minimized(minimized);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_set_tiled(id: ViewId, tiled: LabEdge) {
    if let Some(view) = views_mut().get_view_mut(id) {
        view.set_tiled(tiled);
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn view_offer_focus(id: ViewId) {
    if let Some(view) = views().get_view(id) {
        view.offer_focus();
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
