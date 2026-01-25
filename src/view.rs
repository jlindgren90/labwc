// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::foreign_toplevel::*;
use crate::rect::*;
use crate::view_geom::*;
use crate::view_impl::*;
use std::ffi::CString;
use std::ptr::null_mut;

const FALLBACK_WIDTH: i32 = 640;
const FALLBACK_HEIGHT: i32 = 480;

impl ViewState {
    pub fn focusable(&self) -> bool {
        self.mapped
            && (self.focus_mode == VIEW_FOCUS_MODE_ALWAYS
                || self.focus_mode == VIEW_FOCUS_MODE_LIKELY)
    }

    pub fn floating(&self) -> bool {
        !self.fullscreen && self.maximized == VIEW_AXIS_NONE && self.tiled == LAB_EDGE_NONE
    }
}

impl From<&ViewState> for ForeignToplevelState {
    fn from(state: &ViewState) -> Self {
        ForeignToplevelState {
            maximized: state.maximized == VIEW_AXIS_BOTH,
            minimized: state.minimized,
            activated: state.active,
            fullscreen: state.fullscreen,
        }
    }
}

#[derive(Default)]
struct ViewData {
    app_id: CString,
    title: CString,
    foreign_toplevels: Vec<ForeignToplevel>,
    saved_geom: Rect, // geometry before adjusting for layout change
    in_layout_change: bool,
    lost_output: bool,
}

pub struct View {
    v: Box<dyn ViewImpl>,
    d: ViewData,
    state: Box<ViewState>,
    c_ptr: *mut CView, // TODO: remove
}

impl View {
    pub fn new(c_ptr: *mut CView, is_xwayland: bool) -> Self {
        let mut view = View {
            v: if is_xwayland {
                Box::new(XView::new(c_ptr))
            } else {
                Box::new(XdgView::new(c_ptr))
            },
            d: ViewData::default(),
            state: Box::default(),
            c_ptr: c_ptr,
        };
        view.state.app_id = view.d.app_id.as_ptr(); // for C interop
        view.state.title = view.d.title.as_ptr(); // for C interop
        return view;
    }

    pub fn get_state(&self) -> &ViewState {
        &self.state
    }

    pub fn set_app_id(&mut self, app_id: CString) {
        if self.d.app_id != app_id {
            self.d.app_id = app_id;
            self.state.app_id = self.d.app_id.as_ptr(); // for C interop
            unsafe { view_notify_app_id_change(self.c_ptr) };
            for toplevel in &self.d.foreign_toplevels {
                toplevel.send_app_id(&self.d.app_id);
                toplevel.send_done();
            }
        }
    }

    pub fn set_title(&mut self, title: CString) {
        if self.d.title != title {
            self.d.title = title;
            self.state.title = self.d.title.as_ptr(); // for C interop
            unsafe { view_notify_title_change(self.c_ptr) };
            for toplevel in &self.d.foreign_toplevels {
                toplevel.send_title(&self.d.title);
                toplevel.send_done();
            }
        }
    }

    // Returns CView pointer to pass to view_notify_map()
    pub fn set_mapped(&mut self) -> *mut CView {
        self.state.mapped = true;
        self.state.ever_mapped = true;
        self.state.focus_mode = self.v.get_focus_mode();
        return self.c_ptr;
    }

    // Returns CView pointer to pass to view_notify_unmap()
    pub fn set_unmapped(&mut self) -> *mut CView {
        self.state.mapped = false;
        for resource in self.d.foreign_toplevels.drain(..) {
            resource.close();
        }
        return self.c_ptr;
    }

    pub fn set_active(&mut self, active: bool) {
        if self.state.active != active {
            self.state.active = active;
            self.v.set_active(active);
            unsafe { view_notify_active(self.c_ptr) };
            self.send_foreign_toplevel_state();
        }
    }

    pub fn set_ssd_enabled(&mut self, ssd_enabled: bool) {
        if self.state.ssd_enabled != ssd_enabled {
            self.state.ssd_enabled = ssd_enabled;
            unsafe { view_notify_ssd_enabled(self.c_ptr) };
        }
    }

    fn set_fullscreen(&mut self, fullscreen: bool) {
        if self.state.fullscreen != fullscreen {
            self.state.fullscreen = fullscreen;
            self.v.set_fullscreen(fullscreen);
            self.send_foreign_toplevel_state();
        }
    }

    pub fn set_maximized(&mut self, maximized: ViewAxis) {
        if self.state.maximized != maximized {
            self.state.maximized = maximized;
            self.v.set_maximized(maximized);
            self.send_foreign_toplevel_state();
        }
    }

    pub fn set_tiled(&mut self, tiled: LabEdge) {
        if self.state.tiled != tiled {
            self.state.tiled = tiled;
            self.v.notify_tiled();
        }
    }

    pub fn set_minimized(&mut self, minimized: bool) {
        if self.state.minimized != minimized {
            self.state.minimized = minimized;
            self.v.set_minimized(minimized);
            self.send_foreign_toplevel_state();
        }
    }

    pub fn set_current_pos(&mut self, x: i32, y: i32) {
        self.state.current.x = x;
        self.state.current.y = y;
    }

    pub fn set_current_size(&mut self, width: i32, height: i32) {
        self.state.current.width = width;
        self.state.current.height = height;
    }

    pub fn set_pending_geom(&mut self, geom: Rect) {
        self.state.pending = geom;
    }

    pub fn move_resize(&mut self, geom: Rect) {
        if rect_equals(self.state.pending, geom) {
            return;
        }
        self.v
            .configure(geom, &mut self.state.pending, &mut self.state.current);
        if self.state.floating() {
            // Moving a floating view also sets the output
            self.state.output = nearest_output_to_geom(self.state.pending);
        }
        if !self.d.in_layout_change {
            // User-initiated move/resize invalidates saved geometry
            self.d.saved_geom = Rect::default();
            self.d.lost_output = false;
        }
    }

    pub fn set_fallback_natural_geom(&mut self) {
        let mut natural = Rect {
            width: FALLBACK_WIDTH,
            height: FALLBACK_HEIGHT,
            ..self.state.natural_geom
        };
        compute_default_geom(&*self.state, &mut natural, Rect::default(), false);
        self.state.natural_geom = natural;
    }

    pub fn store_natural_geom(&mut self) {
        // Don't save natural geometry if fullscreen or tiled
        if self.state.fullscreen || self.state.tiled != 0 {
            return;
        }
        // If only one axis is maximized, save geometry of the other
        if self.state.maximized == VIEW_AXIS_NONE || self.state.maximized == VIEW_AXIS_VERTICAL {
            self.state.natural_geom.x = self.state.pending.x;
            self.state.natural_geom.width = self.state.pending.width;
        }
        if self.state.maximized == VIEW_AXIS_NONE || self.state.maximized == VIEW_AXIS_HORIZONTAL {
            self.state.natural_geom.y = self.state.pending.y;
            self.state.natural_geom.height = self.state.pending.height;
        }
    }

    fn apply_natural_geom(&mut self) {
        let mut natural = self.state.natural_geom;
        ensure_geom_onscreen(&*self.state, &mut natural);
        self.move_resize(natural);
    }

    pub fn apply_special_geom(&mut self) {
        let geom;
        if self.state.fullscreen {
            geom = unsafe { output_layout_coords(self.state.output) };
        } else if self.state.maximized != VIEW_AXIS_NONE {
            geom = compute_maximized_geom(&*self.state);
        } else if self.state.tiled != 0 {
            geom = compute_tiled_geom(&*self.state);
        } else {
            return; // defensive
        }
        if !rect_empty(geom) {
            self.move_resize(geom);
        }
    }

    pub fn set_output(&mut self, output: *mut Output) {
        self.state.output = output;
    }

    pub fn adjust_for_layout_change(&mut self) {
        // Save user geometry prior to first layout-change adjustment
        if rect_empty(self.d.saved_geom) {
            self.d.saved_geom = self.state.pending;
        }
        self.d.in_layout_change = true;
        self.d.lost_output |= unsafe { !output_is_usable(self.state.output) };
        // Keep non-floating views on the same output if possible
        let is_floating = self.state.floating();
        if is_floating || self.d.lost_output {
            self.state.output = nearest_output_to_geom(self.d.saved_geom);
        }
        if !is_floating {
            self.apply_special_geom();
        } else if unsafe { view_has_strut_partial(self.c_ptr) } {
            // Do not move panels etc. out of their own reserved area
        } else {
            // Restore saved geometry, ensuring view is on-screen
            let mut geom = self.d.saved_geom;
            ensure_geom_onscreen(&*self.state, &mut geom);
            self.move_resize(geom);
        }
        self.d.in_layout_change = false;
    }

    // Returns CView pointer to pass to view_notify_fullscreen()
    pub fn fullscreen(&mut self, fullscreen: bool) -> *mut CView {
        if self.state.fullscreen == fullscreen {
            return null_mut();
        }
        // Fullscreening ends any interactive move/resize
        if fullscreen {
            unsafe { interactive_cancel(self.c_ptr) };
            self.store_natural_geom();
        }
        self.set_fullscreen(fullscreen);
        if self.state.floating() {
            self.apply_natural_geom();
        } else {
            self.apply_special_geom();
        }
        return self.c_ptr;
    }

    pub fn maximize(&mut self, axis: ViewAxis) {
        if self.state.maximized == axis {
            return;
        }
        // In snap-to-maximize case, natural geometry was already stored
        let store_natural_geometry = unsafe { !interactive_move_is_active(self.c_ptr) };
        // Maximizing/unmaximizing ends any interactive move/resize
        unsafe { interactive_cancel(self.c_ptr) };
        if store_natural_geometry {
            self.store_natural_geom();
        }
        // Corner case: if unmaximizing one axis but natural geometry is
        // unknown (e.g. for an initially maximized xdg-shell view), we
        // can't request geometry from the client, so use a fallback
        if (axis == VIEW_AXIS_HORIZONTAL || axis == VIEW_AXIS_VERTICAL)
            && rect_empty(self.state.natural_geom)
        {
            self.set_fallback_natural_geom();
        }
        self.set_maximized(axis);
        if self.state.floating() {
            self.apply_natural_geom();
        } else {
            self.apply_special_geom();
        }
    }

    pub fn tile(&mut self, edge: LabEdge) {
        if self.state.tiled == edge {
            return;
        }
        // In snap-to-tile case, natural geometry was already stored
        let store_natural_geometry = unsafe { !interactive_move_is_active(self.c_ptr) };
        // Tiling ends any interactive move/resize
        if edge != LAB_EDGE_NONE {
            unsafe { interactive_cancel(self.c_ptr) };
        }
        if store_natural_geometry {
            self.store_natural_geom();
        }
        self.set_tiled(edge);
        if self.state.floating() {
            self.apply_natural_geom();
        } else {
            self.apply_special_geom();
        }
    }

    pub fn offer_focus(&self) {
        self.v.offer_focus();
    }

    pub fn add_foreign_toplevel(&mut self, client: *mut WlResource) {
        let toplevel = ForeignToplevel::new(client, self.c_ptr);
        toplevel.send_app_id(&self.d.app_id);
        toplevel.send_title(&self.d.title);
        toplevel.send_state((&*self.state).into());
        toplevel.send_done();
        self.d.foreign_toplevels.push(toplevel);
    }

    pub fn remove_foreign_toplevel(&mut self, resource: *mut WlResource) {
        self.d.foreign_toplevels.retain(|t| t.res != resource);
    }

    fn send_foreign_toplevel_state(&self) {
        for toplevel in &self.d.foreign_toplevels {
            toplevel.send_state((&*self.state).into());
            toplevel.send_done();
        }
    }
}
