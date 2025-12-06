include!("../build/include/view-c.rs");

use lazy_static;
use std::collections::HashMap;
use std::ffi::{c_char, CString};
use util::cstring;

// Directions in which a view can be maximized. "None" is used
// internally to mean "not maximized" but is not valid in rc.xml.
// Therefore when parsing rc.xml, "None" means "Invalid".
//
#[repr(C)]
#[derive(Clone, Default, PartialEq)]
#[allow(dead_code)]
pub enum ViewAxis {
    #[default]
    None = 0,
    Horizontal = 1 << 0,
    Vertical = 1 << 1,
    Both = (1 << 0) | (1 << 1),
    Invalid = 1 << 2,
}

pub type ViewId = u64;

#[repr(C)]
#[derive(Default)]
pub struct ViewState {
    app_id: *const c_char,
    title: *const c_char,
    fullscreen: bool,
    maximized: ViewAxis,
    minimized: bool,
}

#[derive(Default)]
pub struct View {
    c_ptr: *mut CView,
    is_xwayland: bool,
    app_id: CString,
    title: CString,
    state: ViewState,
}

#[derive(Default)]
pub struct Views {
    by_id: HashMap<ViewId, View>,
    max_id: ViewId,
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[no_mangle]
pub extern "C" fn view_add(c_ptr: *mut CView, is_xwayland: bool) -> ViewId {
    let mut views = views_mut();
    views.max_id += 1;
    let id = views.max_id;
    let mut view = View {
        c_ptr: c_ptr,
        is_xwayland: is_xwayland,
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
        &view.state
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
            unsafe {
                view_notify_app_id_change(view.c_ptr);
            }
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
            unsafe {
                view_notify_title_change(view.c_ptr);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_fullscreen_internal(id: ViewId, fullscreen: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        unsafe {
            if view.is_xwayland {
                xwayland_view_set_fullscreen(view.c_ptr, fullscreen as i32);
            } else {
                xdg_toplevel_view_set_fullscreen(view.c_ptr, fullscreen as i32);
            }
        }
        view.state.fullscreen = fullscreen;
    }
}

// Sets maximized state without updating geometry. Used in interactive
// move/resize. In most other cases, use view_maximize() instead.
//
#[no_mangle]
pub extern "C" fn view_set_maximized(id: ViewId, maximized: ViewAxis) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        if view.state.maximized == maximized {
            return;
        }
        unsafe {
            if view.is_xwayland {
                xwayland_view_maximize(view.c_ptr, maximized.clone() as i32);
            } else {
                xdg_toplevel_view_maximize(view.c_ptr, maximized.clone() as i32);
            }
        }
        view.state.maximized = maximized;
        unsafe {
            view_notify_maximized(view.c_ptr);
        }
    }
}

#[no_mangle]
pub extern "C" fn view_minimize_internal(id: ViewId, minimized: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        unsafe {
            if view.is_xwayland {
                xwayland_view_minimize(view.c_ptr, minimized as i32);
            } else {
                // no-op for xdg-shell view
            }
        }
        view.state.minimized = minimized;
    }
}

#[no_mangle]
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().by_id.remove(&id);
}
