// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::foreign_toplevel::*;
use crate::rect::*;
use crate::view_geom::*;
use std::ffi::CString;

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
pub struct View {
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
    pub fn new(c_ptr: *mut CView, is_xwayland: bool) -> Self {
        let mut view = View {
            c_ptr: c_ptr,
            is_xwayland: is_xwayland,
            ..View::default()
        };
        view.state.app_id = view.app_id.as_ptr(); // for C interop
        view.state.title = view.title.as_ptr(); // for C interop
        return view;
    }

    pub fn get_state(&self) -> &ViewState {
        &self.state
    }

    pub fn set_app_id(&mut self, app_id: CString) {
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

    pub fn set_title(&mut self, title: CString) {
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

    // Returns CView pointer to pass to view_notify_map()
    pub fn set_mapped(&mut self, focus_mode: ViewFocusMode) -> *mut CView {
        self.state.mapped = true;
        self.state.ever_mapped = true;
        self.state.focus_mode = focus_mode;
        return self.c_ptr;
    }

    // Returns CView pointer to pass to view_notify_unmap()
    pub fn set_unmapped(&mut self) -> *mut CView {
        self.state.mapped = false;
        for resource in self.foreign_toplevels.drain(..) {
            resource.close();
        }
        return self.c_ptr;
    }

    pub fn set_active(&mut self, active: bool) {
        if self.state.active != active {
            self.state.active = active;
            if self.is_xwayland {
                unsafe { xwayland_view_set_active(self.c_ptr, active) };
            } else {
                unsafe { xdg_toplevel_view_set_active(self.c_ptr, active) };
            }
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

    pub fn set_fullscreen(&mut self, fullscreen: bool) {
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

    pub fn set_maximized(&mut self, maximized: ViewAxis) {
        if self.state.maximized != maximized {
            self.state.maximized = maximized;
            if self.is_xwayland {
                unsafe { xwayland_view_maximize(self.c_ptr, maximized) };
            } else {
                unsafe { xdg_toplevel_view_maximize(self.c_ptr, maximized) };
            }
            self.send_foreign_toplevel_state();
        }
    }

    pub fn set_tiled(&mut self, tiled: LabEdge) {
        if self.state.tiled != tiled {
            self.state.tiled = tiled;
            if !self.is_xwayland {
                unsafe { xdg_toplevel_view_notify_tiled(self.c_ptr) };
            }
        }
    }

    pub fn set_minimized(&mut self, minimized: bool) {
        if self.state.minimized != minimized {
            self.state.minimized = minimized;
            if self.is_xwayland {
                unsafe { xwayland_view_minimize(self.c_ptr, minimized) };
            }
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
        if self.state.floating() {
            // Moving a floating view also sets the output
            self.state.output = nearest_output_to_geom(self.state.pending);
        }
        if !self.in_layout_change {
            // User-initiated move/resize invalidates saved geometry
            self.saved_geom = Rect::default();
            self.lost_output = false;
        }
    }

    // Used only for xwayland views
    pub fn adjust_initial_geom(&mut self, keep_position: bool) {
        if self.state.floating() {
            let mut geom = self.state.pending;
            compute_default_geom(&*self.state, &mut geom, Rect::default(), keep_position);
            self.move_resize(geom);
        } else {
            let mut natural = self.state.natural_geom;
            // A maximized/fullscreen view should have a reasonable natural geometry
            if natural.width < MIN_WIDTH || natural.height < MIN_HEIGHT {
                natural.width = FALLBACK_WIDTH;
                natural.height = FALLBACK_HEIGHT;
            }
            // FIXME: use border widths for floating state here
            compute_default_geom(&*self.state, &mut natural, Rect::default(), keep_position);
            self.state.natural_geom = natural;
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

    pub fn apply_natural_geom(&mut self) {
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
        if rect_empty(self.saved_geom) {
            self.saved_geom = self.state.pending;
        }
        self.in_layout_change = true;
        self.lost_output |= unsafe { !output_is_usable(self.state.output) };
        // Keep non-floating views on the same output if possible
        let is_floating = self.state.floating();
        if is_floating || self.lost_output {
            self.state.output = nearest_output_to_geom(self.saved_geom);
        }
        if !is_floating {
            self.apply_special_geom();
        } else if unsafe { view_has_strut_partial(self.c_ptr) } {
            // Do not move panels etc. out of their own reserved area
        } else {
            // Restore saved geometry, ensuring view is on-screen
            let mut geom = self.saved_geom;
            ensure_geom_onscreen(&*self.state, &mut geom);
            self.move_resize(geom);
        }
        self.in_layout_change = false;
    }

    pub fn offer_focus(&self) {
        if self.is_xwayland {
            unsafe { xwayland_view_offer_focus(self.c_ptr) };
        }
    }

    pub fn add_foreign_toplevel(&mut self, client: *mut WlResource) {
        let toplevel = ForeignToplevel::new(client, self.c_ptr);
        toplevel.send_app_id(&self.app_id);
        toplevel.send_title(&self.title);
        toplevel.send_state((&*self.state).into());
        toplevel.send_done();
        self.foreign_toplevels.push(toplevel);
    }

    pub fn remove_foreign_toplevel(&mut self, resource: *mut WlResource) {
        self.foreign_toplevels.retain(|t| t.res != resource);
    }

    fn send_foreign_toplevel_state(&self) {
        for toplevel in &self.foreign_toplevels {
            toplevel.send_state((&*self.state).into());
            toplevel.send_done();
        }
    }
}
