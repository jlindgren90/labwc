// SPDX-License-Identifier: GPL-2.0-only
//
use bindings::*;
use foreign_toplevel::ForeignToplevel;
use lazy_static;
use std::cmp::min;
use std::collections::BTreeMap;
use std::ffi::{c_char, CString};
use util::*;

// from enum lab_edge
const EDGE_TOP: i32 = 1 << 0;
const EDGE_BOTTOM: i32 = 1 << 1;
const EDGE_LEFT: i32 = 1 << 2;
const EDGE_RIGHT: i32 = 1 << 3;

const FALLBACK_WIDTH: i32 = 640;
const FALLBACK_HEIGHT: i32 = 480;
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
    saved_geom: Rect, // geometry before adjusting for layout change
    in_layout_change: bool,
    lost_output: bool,
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

    fn set_minimized(&mut self, minimized: bool, visibility_changed: &mut bool) {
        if self.state.minimized != minimized {
            self.state.minimized = minimized;
            if self.is_xwayland {
                unsafe { xwayland_view_minimize(self.c_ptr, minimized) };
            }
            self.send_foreign_toplevel_state();
            if self.state.mapped {
                unsafe { view_set_visible(self.c_ptr, !minimized) };
                *visibility_changed = true;
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
        // User-initiated move/resize invalidates saved geometry
        if !self.in_layout_change {
            self.saved_geom = Rect::default();
            self.lost_output = false;
        }
    }

    fn set_fallback_natural_geom(&mut self) {
        let mut natural = Rect {
            width: FALLBACK_WIDTH,
            height: FALLBACK_HEIGHT,
            ..self.state.natural_geom
        };
        self.center_geom(&mut natural, None);
        self.state.natural_geom = natural;
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

    fn apply_natural_geom(&mut self) {
        let mut natural = self.state.natural_geom;
        self.ensure_geom_onscreen(&mut natural);
        self.move_resize(natural);
    }

    fn apply_fullscreen_geom(&mut self) {
        let geom = unsafe { view_get_output_area(self.c_ptr) };
        if !rect_empty(geom) {
            self.move_resize(geom);
        }
    }

    fn apply_maximized_geom(&mut self) {
        let usable = unsafe { view_get_output_usable_area(self.c_ptr) };
        let margin = unsafe { ssd_get_margin(self.c_ptr) };
        let mut geom = Rect {
            x: usable.x + margin.left,
            y: usable.y + margin.top,
            width: usable.width - (margin.left + margin.right),
            height: usable.height - (margin.top + margin.bottom),
        };
        // If one axis (horizontal or vertical) is unmaximized, it should
        // use the natural geometry (first ensuring it is on-screen)
        if self.state.maximized != ViewAxis::Both {
            let mut natural = self.state.natural_geom;
            self.ensure_geom_onscreen(&mut natural);
            if self.state.maximized == ViewAxis::Vertical {
                geom.x = natural.x;
                geom.width = natural.width;
            } else if self.state.maximized == ViewAxis::Horizontal {
                geom.y = natural.y;
                geom.height = natural.height;
            }
        }
        if !rect_empty(geom) {
            self.move_resize(geom);
        }
    }

    fn apply_tiled_geom(&mut self) {
        let usable = unsafe { view_get_output_usable_area(self.c_ptr) };
        let margin = unsafe { ssd_get_margin(self.c_ptr) };
        let (mut x1, mut x2) = (0, usable.width);
        let (mut y1, mut y2) = (0, usable.height);
        if (self.state.tiled & EDGE_RIGHT) != 0 {
            x1 = usable.width / 2;
        }
        if (self.state.tiled & EDGE_LEFT) != 0 {
            x2 = usable.width / 2;
        }
        if (self.state.tiled & EDGE_BOTTOM) != 0 {
            y1 = usable.height / 2;
        }
        if (self.state.tiled & EDGE_TOP) != 0 {
            y2 = usable.height / 2;
        }
        let geom = Rect {
            x: usable.x + x1 + margin.left,
            y: usable.y + y1 + margin.top,
            width: x2 - x1 - (margin.left + margin.right),
            height: y2 - y1 - (margin.top + margin.bottom),
        };
        if !rect_empty(geom) {
            self.move_resize(geom);
        }
    }

    fn apply_special_geom(&mut self) {
        if self.state.fullscreen {
            self.apply_fullscreen_geom();
        } else if self.state.maximized != ViewAxis::None {
            self.apply_maximized_geom();
        } else if self.state.tiled != 0 {
            self.apply_tiled_geom();
        }
    }

    fn adjust_for_layout_change(&mut self) {
        // Save user geometry prior to first layout-change adjustment
        if rect_empty(self.saved_geom) {
            self.saved_geom = self.state.pending;
        }
        self.in_layout_change = true;
        self.lost_output |= unsafe { !view_has_usable_output(self.c_ptr) };
        // Keep non-floating views on the same output if possible
        let is_floating = view_is_floating(&*self.state);
        if is_floating || self.lost_output {
            unsafe { view_discover_output(self.c_ptr, &self.saved_geom) };
        }
        if is_floating {
            // Restore saved geometry, ensuring view is on-screen
            let mut geom = self.saved_geom;
            self.ensure_geom_onscreen(&mut geom);
            self.move_resize(geom);
        } else {
            self.apply_special_geom();
        }
        self.in_layout_change = false;
    }

    fn maximize(&mut self, axis: ViewAxis) {
        if self.state.maximized == axis {
            return;
        }
        // In snap-to-maximize case, natural geometry was already stored
        let store_natural_geometry = unsafe { !interactive_move_is_active(self.c_ptr) };
        // Maximizing ends any interactive move/resize
        if axis != ViewAxis::None {
            unsafe { interactive_cancel(self.c_ptr) };
        }
        if store_natural_geometry {
            self.store_natural_geom();
        }
        // Corner case: if unmaximizing one axis but natural geometry is
        // unknown (e.g. for an initially maximized xdg-shell view), we
        // can't request geometry from the client, so use a fallback
        if (axis == ViewAxis::Horizontal || axis == ViewAxis::Vertical)
            && rect_empty(self.state.natural_geom)
        {
            self.set_fallback_natural_geom();
        }
        self.set_maximized(axis);
        if view_is_floating(&*self.state) {
            self.apply_natural_geom();
        } else {
            self.apply_special_geom();
        }
    }

    fn fullscreen(&mut self, fullscreen: bool) {
        if self.state.fullscreen == fullscreen {
            return;
        }
        // Fullscreening ends any interactive move/resize
        if fullscreen {
            unsafe { interactive_cancel(self.c_ptr) };
            self.store_natural_geom();
        }
        self.set_fullscreen(fullscreen);
        if view_is_floating(&*self.state) {
            self.apply_natural_geom();
        } else {
            self.apply_special_geom();
        }
        unsafe { view_notify_fullscreen(self.c_ptr) };
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
        if !view.state.minimized {
            let view_ptr = view.c_ptr;
            drop(views); // FIXME: to allow reentrant borrow
            unsafe { view_set_visible(view_ptr, true) };
            unsafe { view_notify_visible(view_ptr) };
        }
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
        if !view.state.minimized {
            let view_ptr = view.c_ptr;
            drop(views); // FIXME: to allow reentrant borrow
            unsafe { view_set_visible(view_ptr, false) };
            unsafe { view_notify_visible(view_ptr) };
        }
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
pub extern "C" fn view_adjust_for_layout_change(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.adjust_for_layout_change();
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
pub extern "C" fn view_maximize(id: ViewId, axis: ViewAxis) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.maximize(axis);
    }
}

#[no_mangle]
pub extern "C" fn view_fullscreen(id: ViewId, fullscreen: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.fullscreen(fullscreen);
    }
}

#[no_mangle]
pub extern "C" fn view_minimize(id: ViewId, minimized: bool) {
    let mut views = views_mut();
    if let Some(view) = views.by_id.get(&id) {
        if view.state.minimized == minimized {
            return;
        }
        // Minimize/unminimize all related views together
        let view_ptr = view.c_ptr;
        let root_ptr = unsafe { view_get_root(view_ptr) };
        let mut visibility_changed = false;
        for view in views.by_id.values_mut() {
            if unsafe { view_get_root(view.c_ptr) } == root_ptr {
                view.set_minimized(minimized, &mut visibility_changed);
            }
        }
        if visibility_changed {
            drop(views); // FIXME: to allow reentrant borrow
            unsafe { view_notify_visible(view_ptr) };
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
