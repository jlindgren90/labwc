use bindings::*;
use foreign_toplevel::ForeignToplevel;
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

// Whether a view wants focus. "Likely" and "Unlikely" apply only to
// XWayland views using the Globally Active input model per the ICCCM.
// These views are offered focus and will voluntarily accept or decline.
//
// In some cases, labwc needs to decide in advance whether to focus a
// view. For this purpose, these views are classified (by a heuristic)
// as likely or unlikely to want focus. However, it is still ultimately
// up to the client whether the view gets focus or not.
//
#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq)]
#[allow(dead_code)]
pub enum ViewFocusMode {
    #[default]
    Never = 0,
    Always,
    Likely,
    Unlikely,
}

pub type ViewId = u64;

#[repr(C)]
#[derive(Default)]
pub struct ViewState {
    app_id: *const c_char,
    title: *const c_char,
    // Geometry of the wlr_surface contained within the view, as currently
    // displayed. Should be kept in sync with the scene-graph at all times.
    current: WlrBox,
    // Expected geometry after any pending move/resize requests have
    // been processed. Should match current geometry when no move/resize
    // requests are pending.
    pending: WlrBox,
    // Saved geometry which will be restored when the view returns to
    // normal/floating state after being maximized/fullscreen/tiled.
    // Values are undefined/out-of-date for floating views.
    natural_geom: WlrBox,
    // Geometry of the view prior to adjusting for layout changes. This
    // is valid only when the most recent move/resize of the view was
    // due to a layout change, and is invalidated by any user-initiated
    // move/resize.
    saved_geom: WlrBox,
    saved_geom_valid: bool,
    lost_output: bool, // also reset by user move/resize
    mapped: bool,
    ever_mapped: bool,
    focus_mode: ViewFocusMode,
    activated: bool,
    fullscreen: bool,
    maximized: ViewAxis,
    minimized: bool,
    tiled: i32, // enum lab_edge
}

#[no_mangle]
pub extern "C" fn view_is_focusable(state: &ViewState) -> bool {
    state.mapped
        && (state.focus_mode == ViewFocusMode::Always || state.focus_mode == ViewFocusMode::Likely)
}

impl From<&ViewState> for ForeignToplevelState {
    fn from(state: &ViewState) -> Self {
        ForeignToplevelState {
            maximized: state.maximized == ViewAxis::Both,
            minimized: state.minimized,
            activated: state.activated,
            fullscreen: state.fullscreen,
        }
    }
}

#[derive(Default)]
struct View {
    c_ptr: *mut CView,
    is_xwayland: bool,
    app_id: CString,
    title: CString,
    state: Box<ViewState>,
    foreign_toplevels: Vec<ForeignToplevel>,
}

impl View {
    fn add_foreign_toplevel(&mut self, client: *mut WlResource) {
        let toplevel = ForeignToplevel::new(client, self.c_ptr);
        toplevel.send_app_id(&self.app_id);
        toplevel.send_title(&self.title);
        toplevel.send_state((&*self.state).into());
        toplevel.send_done();
        self.foreign_toplevels.push(toplevel);
    }

    fn send_foreign_toplevel_state(&self) {
        for toplevel in &self.foreign_toplevels {
            toplevel.send_state((&*self.state).into());
            toplevel.send_done();
        }
    }
}

#[derive(Default)]
struct Views {
    by_id: BTreeMap<ViewId, View>,
    max_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
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
pub extern "C" fn view_add_foreign_toplevel_client(client: *mut WlResource) {
    let Views {
        by_id,
        foreign_toplevel_clients,
        ..
    } = &mut *views_mut();
    foreign_toplevel_clients.push(client);
    for view in by_id.values_mut() {
        if view_is_focusable(&view.state) {
            view.add_foreign_toplevel(client);
        }
    }
}

#[no_mangle]
pub extern "C" fn view_remove_foreign_toplevel_client(client: *mut WlResource) {
    views_mut()
        .foreign_toplevel_clients
        .retain(|&c| c != client);
}

#[no_mangle]
pub extern "C" fn view_remove_foreign_toplevel(id: ViewId, resource: *mut WlResource) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.foreign_toplevels.retain(|t| t.res != resource);
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
            for toplevel in &view.foreign_toplevels {
                toplevel.send_app_id(&view.app_id);
                toplevel.send_done();
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
            unsafe { view_notify_title_change(view.c_ptr) };
            for toplevel in &view.foreign_toplevels {
                toplevel.send_title(&view.title);
                toplevel.send_done();
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_current_pos(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.current.x = x;
        view.state.current.y = y;
    }
}

#[no_mangle]
pub extern "C" fn view_set_current_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.current.width = width;
        view.state.current.height = height;
    }
}

#[no_mangle]
pub extern "C" fn view_set_pending_pos(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.pending.x = x;
        view.state.pending.y = y;
    }
}

#[no_mangle]
pub extern "C" fn view_set_pending_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.pending.width = width;
        view.state.pending.height = height;
    }
}

#[no_mangle]
pub extern "C" fn view_set_natural_geom(id: ViewId, geom: WlrBox) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.natural_geom = geom;
    }
}

#[no_mangle]
pub extern "C" fn view_store_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        // Do not overwrite the stored geometry if fullscreen or tiled.
        // Maximized views are handled on a per-axis basis (see below).
        if view.state.fullscreen || view.state.tiled != 0 {
            return;
        }
        // Note that for xdg-shell views that start fullscreen or maximized,
        // we end up storing a natural geometry of 0x0. This is intentional.
        // When leaving fullscreen or unmaximizing, we pass 0x0 to the
        // xdg-toplevel configure event, which means the application should
        // choose its own size.
        if view.state.maximized == ViewAxis::None || view.state.maximized == ViewAxis::Vertical {
            view.state.natural_geom.x = view.state.pending.x;
            view.state.natural_geom.width = view.state.pending.width;
        }
        if view.state.maximized == ViewAxis::None || view.state.maximized == ViewAxis::Horizontal {
            view.state.natural_geom.y = view.state.pending.y;
            view.state.natural_geom.height = view.state.pending.height;
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_saved_geom(id: ViewId, geom: WlrBox) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.saved_geom = geom;
    }
}

#[no_mangle]
pub extern "C" fn view_set_saved_geom_valid(id: ViewId, valid: bool, lost_output: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.saved_geom_valid = valid;
        view.state.lost_output = lost_output;
    }
}

#[no_mangle]
pub extern "C" fn view_set_mapped(id: ViewId, focus_mode: ViewFocusMode) {
    let Views {
        by_id,
        foreign_toplevel_clients,
        ..
    } = &mut *views_mut();
    if let Some(view) = by_id.get_mut(&id) {
        view.state.mapped = true;
        view.state.ever_mapped = true;
        view.state.focus_mode = focus_mode;
        // Create foreign-toplevel handles. Exclude unfocusable views
        // (popups, floating toolbars, etc.) as these should not be
        // shown in taskbars/docks/etc.
        if view_is_focusable(&view.state) {
            for &mut client in foreign_toplevel_clients {
                view.add_foreign_toplevel(client);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_unmapped(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.mapped = false;
        for resource in view.foreign_toplevels.drain(..) {
            resource.close();
        }
    }
}

// For use by desktop_focus_view() only - please do not call directly.
// See comments on ViewFocusMode for more information.
//
#[no_mangle]
pub extern "C" fn view_offer_focus(id: ViewId) {
    if let Some(view) = views().by_id.get(&id) {
        if view.is_xwayland {
            unsafe { xwayland_view_offer_focus(view.c_ptr) };
        }
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
            view.send_foreign_toplevel_state();
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
            view.send_foreign_toplevel_state();
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
            view.send_foreign_toplevel_state();
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
            view.send_foreign_toplevel_state();
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
