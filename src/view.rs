// SPDX-License-Identifier: GPL-2.0-only
//
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
    None = 0x0,
    Horizontal = 0x1,
    Vertical = 0x2,
    Both = 0x3,
}

// Unique (never re-used) ID for each view
pub type ViewId = u64;

#[repr(C)]
#[derive(Default)]
pub struct ViewState {
    app_id: *const c_char, // points to View::app_id
    title: *const c_char,  // points to View::title
    mapped: bool,
    ever_mapped: bool,
    activated: bool,
    fullscreen: bool,
    maximized: ViewAxis,
    minimized: bool,
    tiled: i32, // enum lab_edge
}

#[no_mangle]
pub extern "C" fn view_is_floating(state: &ViewState) -> bool {
    !state.fullscreen && state.maximized == ViewAxis::None && state.tiled == 0
}

#[derive(Default)]
struct View {
    c_ptr: *mut CView,
    is_xwayland: bool,
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

    fn set_activated(&mut self, activated: bool) {
        if self.state.activated != activated {
            self.state.activated = activated;
            if self.is_xwayland {
                unsafe { xwayland_view_set_activated(self.c_ptr, activated) };
            } else {
                unsafe { xdg_toplevel_view_set_activated(self.c_ptr, activated) };
            }
        }
    }

    fn set_fullscreen(&mut self, fullscreen: bool) {
        if self.state.fullscreen != fullscreen {
            self.state.fullscreen = fullscreen;
            if self.is_xwayland {
                unsafe { xwayland_view_set_fullscreen(self.c_ptr, fullscreen) };
            } else {
                unsafe { xdg_toplevel_view_set_fullscreen(self.c_ptr, fullscreen) };
            }
        }
    }

    fn set_maximized(&mut self, maximized: ViewAxis) {
        if self.state.maximized != maximized {
            self.state.maximized = maximized;
            if self.is_xwayland {
                unsafe { xwayland_view_maximize(self.c_ptr, maximized as i32) };
            } else {
                unsafe { xdg_toplevel_view_maximize(self.c_ptr, maximized as i32) };
            }
            unsafe { view_notify_maximized(self.c_ptr) };
        }
    }

    fn set_minimized(&mut self, minimized: bool) {
        if self.state.minimized != minimized {
            self.state.minimized = minimized;
            if self.is_xwayland {
                unsafe { xwayland_view_minimize(self.c_ptr, minimized) };
            }
        }
    }

    fn set_tiled(&mut self, tiled: i32) {
        if self.state.tiled != tiled {
            self.state.tiled = tiled;
            if !self.is_xwayland {
                unsafe { xdg_toplevel_view_notify_tiled(self.c_ptr) };
            }
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
pub extern "C" fn view_add(c_ptr: *mut CView, is_xwayland: bool) -> ViewId {
    let mut views = views_mut();
    views.max_used_id += 1;
    let id = views.max_used_id;
    let mut view = View {
        c_ptr: c_ptr,
        is_xwayland: is_xwayland,
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
pub extern "C" fn view_map_common(id: ViewId) {
    let mut views = views_mut();
    if let Some(view) = views.by_id.get_mut(&id) {
        view.state.mapped = true;
        view.state.ever_mapped = true;
        let view_ptr = view.c_ptr;
        drop(views); // FIXME: to allow reentrant borrow
        unsafe { view_notify_map(view_ptr) };
    }
}

#[no_mangle]
pub extern "C" fn view_unmap_common(id: ViewId) {
    let mut views = views_mut();
    if let Some(view) = views.by_id.get_mut(&id) {
        view.state.mapped = false;
        let view_ptr = view.c_ptr;
        drop(views); // FIXME: to allow reentrant borrow
        unsafe { view_notify_unmap(view_ptr) };
    }
}

#[no_mangle]
pub extern "C" fn view_set_activated_internal(id: ViewId, activated: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_activated(activated);
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
pub extern "C" fn view_set_tiled(id: ViewId, tiled: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_tiled(tiled);
    }
}

#[no_mangle]
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().by_id.remove(&id);
}
