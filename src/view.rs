use bindings::*;
use lazy_static;
use std::collections::BTreeMap;
use std::ffi::{c_char, CString};
use util::cstring;

pub type ViewId = u64;

#[repr(C)]
#[derive(Default)]
pub struct ViewState {
    app_id: *const c_char,
    title: *const c_char,
}

#[derive(Default)]
struct View {
    c_ptr: *mut CView,
    app_id: CString,
    title: CString,
    state: Box<ViewState>,
}

#[derive(Default)]
struct Views {
    by_id: BTreeMap<ViewId, View>,
    max_id: ViewId,
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[no_mangle]
pub extern "C" fn view_add(c_ptr: *mut CView) -> ViewId {
    let mut views = views_mut();
    views.max_id += 1;
    let id = views.max_id;
    let mut view = View {
        c_ptr: c_ptr,
        ..View::default()
    };
    view.state.app_id = view.app_id.as_ptr(); // C interop
    view.state.title = view.title.as_ptr(); // C interop
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
        let app_id = cstring(app_id);
        if view.app_id != app_id {
            view.app_id = app_id;
            view.state.app_id = view.app_id.as_ptr(); // C interop
            unsafe { view_notify_app_id_change(view.c_ptr) };
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_title(id: ViewId, title: *const c_char) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        let title = cstring(title);
        if view.title != title {
            view.title = title;
            view.state.title = view.title.as_ptr(); // C interop
            unsafe { view_notify_title_change(view.c_ptr) };
        }
    }
}

#[no_mangle]
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().by_id.remove(&id);
}
