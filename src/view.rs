// SPDX-License-Identifier: GPL-2.0-only
//
use bindings::*;
use foreign_toplevel::ForeignToplevel;
use lazy_static;
use std::cmp::min;
use std::collections::BTreeMap;
use std::ffi::{c_char, CString};
use util::*;

const MIN_VISIBLE_PX: i32 = 16;

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

// Indicates whether a view wants keyboard focus. Likely and Unlikely
// apply to XWayland views using ICCCM's "Globally Active" input model.
// The client voluntarily decides whether to take focus or not, while
// a heuristic is used to determine whether to show the view in Alt-Tab,
// taskbars, etc.
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

// Unique (never re-used) ID for each view
pub type ViewId = u64;

#[repr(C)]
#[derive(Default)]
pub struct ViewState {
    app_id: *const c_char, // points to View::app_id
    title: *const c_char,  // points to View::title
    mapped: bool,
    ever_mapped: bool,
    focus_mode: ViewFocusMode,
    activated: bool,
    fullscreen: bool,
    maximized: ViewAxis,
    minimized: bool,
    tiled: i32,         // enum lab_edge
    current: Rect,      // current displayed geometry
    pending: Rect,      // expected geometry after any pending move/resize
    natural_geom: Rect, // un-{maximized/fullscreen/tiled} geometry
}

#[no_mangle]
pub extern "C" fn view_is_focusable(state: &ViewState) -> bool {
    state.mapped
        && (state.focus_mode == ViewFocusMode::Always || state.focus_mode == ViewFocusMode::Likely)
}

#[no_mangle]
pub extern "C" fn view_is_floating(state: &ViewState) -> bool {
    !state.fullscreen && state.maximized == ViewAxis::None && state.tiled == 0
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
    fn set_app_id(&mut self, app_id: CString) {
        if self.app_id != app_id {
            self.app_id = app_id;
            self.state.app_id = self.app_id.as_ptr(); // for C interop
            unsafe { view_notify_app_id_change(self.c_ptr) };
            for toplevel in &self.foreign_toplevels {
                toplevel.send_app_id(&self.app_id);
                toplevel.send_done();
            }
        }
    }

    fn set_title(&mut self, title: CString) {
        if self.title != title {
            self.title = title;
            self.state.title = self.title.as_ptr(); // for C interop
            unsafe { view_notify_title_change(self.c_ptr) };
            for toplevel in &self.foreign_toplevels {
                toplevel.send_title(&self.title);
                toplevel.send_done();
            }
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
            self.send_foreign_toplevel_state();
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
            self.send_foreign_toplevel_state();
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
            self.send_foreign_toplevel_state();
        }
    }

    fn set_minimized(&mut self, minimized: bool) {
        if self.state.minimized != minimized {
            self.state.minimized = minimized;
            if self.is_xwayland {
                unsafe { xwayland_view_minimize(self.c_ptr, minimized) };
            }
            self.send_foreign_toplevel_state();
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

    fn center_geom(&self, geom: &mut Rect, rel_to: Option<&Rect>) -> bool {
        let usable = unsafe { view_get_output_usable_area(self.c_ptr) };
        if rect_empty(*geom) || rect_empty(usable) {
            return false;
        }
        let margin = unsafe { ssd_get_margin(self.c_ptr) };
        let width = geom.width + margin.left + margin.right;
        let height = geom.height + margin.top + margin.bottom;
        // If reference box is not given then center to usable area
        let centered = rect_center(width, height, *(rel_to.unwrap_or(&usable)), usable);
        geom.x = centered.x + margin.left;
        geom.y = centered.y + margin.top;
        return true;
    }

    fn ensure_geom_onscreen(&self, geom: &mut Rect) {
        let usable = unsafe { view_get_output_usable_area(self.c_ptr) };
        if rect_empty(*geom) || rect_empty(usable) {
            return;
        }
        // Require a minimum number of pixels to be visible on each edge.
        // If the geometry minus this margin is offscreen, then center it.
        let hmargin = min(MIN_VISIBLE_PX, (geom.width - 1) / 2);
        let vmargin = min(MIN_VISIBLE_PX, (geom.height - 1) / 2);
        let reduced = Rect {
            x: geom.x + hmargin,
            y: geom.y + vmargin,
            width: geom.width - 2 * hmargin,
            height: geom.height - 2 * vmargin,
        };
        if !rect_intersects(reduced, usable) {
            self.center_geom(geom, Some(&usable));
        }
    }

    fn move_resize(&mut self, geom: Rect) {
        if self.is_xwayland {
            unsafe {
                xwayland_view_configure(
                    self.c_ptr,
                    geom,
                    &mut self.state.pending,
                    &mut self.state.current,
                )
            };
        } else {
            unsafe {
                xdg_toplevel_view_configure(
                    self.c_ptr,
                    geom,
                    &mut self.state.pending,
                    &mut self.state.current,
                )
            };
        }
        unsafe { view_notify_move_resize(self.c_ptr) };
    }

    fn store_natural_geom(&mut self) {
        // Don't save natural geometry if fullscreen or tiled
        if self.state.fullscreen || self.state.tiled != 0 {
            return;
        }
        // If only one axis is maximized, save geometry of the other
        if self.state.maximized == ViewAxis::None || self.state.maximized == ViewAxis::Vertical {
            self.state.natural_geom.x = self.state.pending.x;
            self.state.natural_geom.width = self.state.pending.width;
        }
        if self.state.maximized == ViewAxis::None || self.state.maximized == ViewAxis::Horizontal {
            self.state.natural_geom.y = self.state.pending.y;
            self.state.natural_geom.height = self.state.pending.height;
        }
    }

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
    by_id: BTreeMap<ViewId, View>, // can be iterated in creation-order
    max_used_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
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
pub extern "C" fn view_map_common(id: ViewId, focus_mode: ViewFocusMode) {
    let mut views = views_mut();
    let Views {
        by_id,
        foreign_toplevel_clients,
        ..
    } = &mut *views;
    if let Some(view) = by_id.get_mut(&id) {
        view.state.mapped = true;
        view.state.ever_mapped = true;
        view.state.focus_mode = focus_mode;
        // Only focusable views should be shown in taskbars etc.
        if view_is_focusable(&view.state) {
            for &mut client in foreign_toplevel_clients {
                view.add_foreign_toplevel(client);
            }
        }
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
        for resource in view.foreign_toplevels.drain(..) {
            resource.close();
        }
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
pub extern "C" fn view_center_geom(id: ViewId, geom: &mut Rect, rel_to: Option<&Rect>) -> bool {
    if let Some(view) = views().by_id.get(&id) {
        view.center_geom(geom, rel_to)
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn view_ensure_geom_onscreen(id: ViewId, geom: &mut Rect) {
    if let Some(view) = views().by_id.get(&id) {
        view.ensure_geom_onscreen(geom);
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
pub extern "C" fn view_move_resize(id: ViewId, geom: Rect) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.move_resize(geom);
    }
}

#[no_mangle]
pub extern "C" fn view_set_natural_geom(id: ViewId, geom: Rect) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.state.natural_geom = geom;
    }
}

#[no_mangle]
pub extern "C" fn view_store_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.store_natural_geom();
    }
}

#[no_mangle]
pub extern "C" fn view_offer_focus(id: ViewId) {
    if let Some(view) = views().by_id.get(&id) {
        if view.is_xwayland {
            unsafe { xwayland_view_offer_focus(view.c_ptr) };
        }
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
        if view_is_focusable(&view.state) {
            view.add_foreign_toplevel(client);
        }
    }
}

#[no_mangle]
pub extern "C" fn views_remove_foreign_toplevel_client(client: *mut WlResource) {
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
pub extern "C" fn view_remove(id: ViewId) {
    views_mut().by_id.remove(&id);
}
