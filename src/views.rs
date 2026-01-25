// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::lazy_static;
use crate::util::*;
use crate::view::*;
use std::collections::BTreeMap;
use std::ffi::c_char;

#[derive(Default)]
struct Views {
    by_id: BTreeMap<ViewId, View>, // in creation order
    max_used_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[no_mangle]
pub extern "C" fn view_add(c_ptr: *mut CView, is_xwayland: bool) -> ViewId {
    let mut views = views_mut();
    views.max_used_id += 1;
    let id = views.max_used_id;
    views.by_id.insert(id, View::new(c_ptr, is_xwayland));
    return id;
}

#[no_mangle]
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().by_id.remove(&id);
}

#[no_mangle]
pub extern "C" fn view_get_state(id: ViewId) -> *const ViewState {
    let views = views();
    let view = views.by_id.get(&id);
    return view.map_or(std::ptr::null(), |v| v.get_state());
}

#[no_mangle]
pub extern "C" fn view_set_app_id(id: ViewId, app_id: *const c_char) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_app_id(cstring(app_id));
    }
}

#[no_mangle]
pub extern "C" fn view_set_title(id: ViewId, title: *const c_char) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_title(cstring(title));
    }
}

#[no_mangle]
pub extern "C" fn view_map_common(id: ViewId, focus_mode: ViewFocusMode) {
    let mut views = views_mut();
    let Views {
        by_id,
        foreign_toplevel_clients,
        ..
    } = &mut *views;
    if let Some(view) = by_id.get_mut(&id) {
        let view_ptr = view.set_mapped(focus_mode);
        // Only focusable views should be shown in taskbars etc.
        if view_is_focusable(view.get_state()) {
            for &mut client in foreign_toplevel_clients {
                view.add_foreign_toplevel(client);
            }
        }
        drop(views); // FIXME: to allow reentrant borrow
        unsafe { view_notify_map(view_ptr) };
    }
}

#[no_mangle]
pub extern "C" fn view_unmap_common(id: ViewId) {
    let mut views = views_mut();
    if let Some(view) = views.by_id.get_mut(&id) {
        let view_ptr = view.set_unmapped();
        drop(views); // FIXME: to allow reentrant borrow
        unsafe { view_notify_unmap(view_ptr) };
    }
}

#[no_mangle]
pub extern "C" fn view_set_active(id: ViewId, active: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_active(active);
    }
}

#[no_mangle]
pub extern "C" fn view_set_fullscreen_internal(id: ViewId, fullscreen: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_fullscreen(fullscreen);
    }
}

#[no_mangle]
pub extern "C" fn view_set_maximized(id: ViewId, maximized: ViewAxis) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_maximized(maximized);
    }
}

#[no_mangle]
pub extern "C" fn view_set_minimized(id: ViewId, minimized: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_minimized(minimized);
    }
}

#[no_mangle]
pub extern "C" fn view_set_tiled(id: ViewId, tiled: LabEdge) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_tiled(tiled);
    }
}

#[no_mangle]
pub extern "C" fn view_ensure_geom_onscreen(id: ViewId, geom: &mut Rect) {
    if let Some(view) = views().by_id.get(&id) {
        view.ensure_geom_onscreen(geom);
    }
}

#[no_mangle]
pub extern "C" fn view_compute_default_geom(id: ViewId, geom: &mut Rect) {
    if let Some(view) = views().by_id.get(&id) {
        view.compute_default_geom(geom, /* rel_to */ None, /* keep_position */ false);
    }
}

#[no_mangle]
pub extern "C" fn view_set_current_pos(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_current_pos(x, y);
    }
}

#[no_mangle]
pub extern "C" fn view_set_current_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_current_size(width, height);
    }
}

#[no_mangle]
pub extern "C" fn view_set_pending_pos(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_pending_pos(x, y);
    }
}

#[no_mangle]
pub extern "C" fn view_set_pending_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_pending_size(width, height);
    }
}

#[no_mangle]
pub extern "C" fn view_move_resize(id: ViewId, geom: Rect) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.move_resize(geom);
    }
}

#[no_mangle]
pub extern "C" fn view_set_initial_geom(id: ViewId, rel_to: Option<&Rect>, keep_position: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_initial_geom(rel_to, keep_position);
    }
}

#[no_mangle]
pub extern "C" fn view_set_fallback_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_fallback_natural_geom();
    }
}

#[no_mangle]
pub extern "C" fn view_store_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.store_natural_geom();
    }
}

#[no_mangle]
pub extern "C" fn view_apply_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.apply_natural_geom();
    }
}

#[no_mangle]
pub extern "C" fn view_apply_special_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.apply_special_geom();
    }
}

#[no_mangle]
pub extern "C" fn view_set_output(id: ViewId, output: *mut Output) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_output(output);
    }
}

#[no_mangle]
pub extern "C" fn views_adjust_for_layout_change() {
    let mut views = views_mut();
    for v in views.by_id.values_mut() {
        v.adjust_for_layout_change();
    }
    drop(views); // FIXME: to allow reentrant borrow
    unsafe { desktop_update_top_layer_visibility() };
}

#[no_mangle]
pub extern "C" fn view_offer_focus(id: ViewId) {
    if let Some(view) = views().by_id.get(&id) {
        view.offer_focus();
    }
}

#[no_mangle]
pub extern "C" fn views_add_foreign_toplevel_client(client: *mut WlResource) {
    let Views {
        by_id,
        foreign_toplevel_clients,
        ..
    } = &mut *views_mut();
    foreign_toplevel_clients.push(client);
    for view in by_id.values_mut() {
        if view_is_focusable(view.get_state()) {
            view.add_foreign_toplevel(client);
        }
    }
}

#[no_mangle]
pub extern "C" fn views_remove_foreign_toplevel_client(client: *mut WlResource) {
    let mut views = views_mut();
    views.foreign_toplevel_clients.retain(|&c| c != client);
}

#[no_mangle]
pub extern "C" fn view_remove_foreign_toplevel(id: ViewId, resource: *mut WlResource) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.remove_foreign_toplevel(resource);
    }
}
