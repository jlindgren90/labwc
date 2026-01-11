use bindings::*;
use lazy_static;
use std::collections::BTreeMap;
use std::ffi::{c_char, CString};
use util::cstring;

#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq)]
#[allow(dead_code)]
pub enum ViewAxis {
    #[default]
    None = 0,
    Horizontal = 1 << 0,
    Vertical = 1 << 1,
    Both = (1 << 0) | (1 << 1),
}

pub type ViewId = u64;

#[repr(C)]
#[derive(Default)]
pub struct ViewState {
    app_id: *const c_char,
    title: *const c_char,
    mapped: bool,
    ever_mapped: bool,
    activated: bool,
    fullscreen: bool,
    maximized: ViewAxis,
    minimized: bool,
    tiled: i32, // enum lab_edge
}

#[derive(Default)]
struct View {
    c_ptr: *mut CView,
    is_xwayland: bool,
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
pub extern "C" fn view_set_mapped(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.mapped = true;
        view.state.ever_mapped = true;
    }
}

#[no_mangle]
pub extern "C" fn view_set_unmapped(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.mapped = false;
    }
}

#[no_mangle]
pub extern "C" fn view_set_activated_internal(id: ViewId, activated: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        if view.state.activated != activated {
            view.state.activated = activated;
            if view.is_xwayland {
                unsafe { xwayland_view_set_activated(view.c_ptr, activated) };
            } else {
                unsafe { xdg_toplevel_view_set_activated(view.c_ptr, activated) };
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_fullscreen_internal(id: ViewId, fullscreen: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        if view.state.fullscreen != fullscreen {
            view.state.fullscreen = fullscreen;
            if view.is_xwayland {
                unsafe { xwayland_view_set_fullscreen(view.c_ptr, fullscreen) };
            } else {
                unsafe { xdg_toplevel_view_set_fullscreen(view.c_ptr, fullscreen) };
            }
        }
    }
}

// Sets maximized state without updating geometry. Used in interactive
// move/resize. In most other cases, use view_maximize() instead.
//
#[no_mangle]
pub extern "C" fn view_set_maximized(id: ViewId, maximized: ViewAxis) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        if view.state.maximized != maximized {
            view.state.maximized = maximized;
            if view.is_xwayland {
                unsafe { xwayland_view_maximize(view.c_ptr, maximized as i32) };
            } else {
                unsafe { xdg_toplevel_view_maximize(view.c_ptr, maximized as i32) };
            }
            unsafe { view_notify_maximized(view.c_ptr) };
        }
    }
}

#[no_mangle]
pub extern "C" fn view_minimize_internal(id: ViewId, minimized: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        if view.state.minimized != minimized {
            view.state.minimized = minimized;
            if view.is_xwayland {
                unsafe { xwayland_view_minimize(view.c_ptr, minimized) };
            } else {
                // no-op for xdg-shell view
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_tiled(id: ViewId, tiled: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        if view.state.tiled != tiled {
            view.state.tiled = tiled;
            if !view.is_xwayland {
                unsafe { xdg_toplevel_view_notify_tiled(view.c_ptr) };
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().by_id.remove(&id);
}
