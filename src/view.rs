// SPDX-License-Identifier: GPL-2.0-only
//
use bindings::*;
use lazy_static;
use std::collections::BTreeMap;
use std::ffi::{c_char, CString};
use util::cstring;

// Unique (never re-used) ID for each view
pub type ViewId = u64;

#[repr(C)]
#[derive(Default)]
pub struct ViewState {
    app_id: *const c_char, // points to View::app_id
    title: *const c_char,  // points to View::title
}

#[derive(Default)]
struct View {
    c_ptr: *mut CView,
    app_id: CString,
    title: CString,
    state: Box<ViewState>,
}

impl View {
    fn set_app_id(&mut self, app_id: CString) {
        if self.app_id != app_id {
            self.app_id = app_id;
            self.state.app_id = self.app_id.as_ptr(); // for C interop
            unsafe { view_notify_app_id_change(self.c_ptr) };
        }
    }

    fn set_title(&mut self, title: CString) {
        if self.title != title {
            self.title = title;
            self.state.title = self.title.as_ptr(); // for C interop
            unsafe { view_notify_title_change(self.c_ptr) };
        }
    }
}

#[derive(Default)]
struct Views {
    by_id: BTreeMap<ViewId, View>, // can be iterated in creation-order
    max_used_id: ViewId,
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[no_mangle]
pub extern "C" fn view_add(c_ptr: *mut CView) -> ViewId {
    let mut views = views_mut();
    views.max_used_id += 1;
    let id = views.max_used_id;
    let mut view = View {
        c_ptr: c_ptr,
        ..View::default()
    };
    view.state.app_id = view.app_id.as_ptr(); // for C interop
    view.state.title = view.title.as_ptr(); // for C interop
    views.by_id.insert(id, view);
    return id;
}

#[no_mangle]
pub extern "C" fn view_get_state(id: ViewId) -> *const ViewState {
    if let Some(view) = views().by_id.get(&id) {
        &*view.state
    } else {
        std::ptr::null()
    }
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
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().by_id.remove(&id);
}
