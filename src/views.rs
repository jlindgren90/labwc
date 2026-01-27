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
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[no_mangle]
pub extern "C" fn view_add(c_ptr: *mut CView) -> ViewId {
    let mut views = views_mut();
    views.max_used_id += 1;
    let id = views.max_used_id;
    views.by_id.insert(id, View::new(c_ptr));
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
