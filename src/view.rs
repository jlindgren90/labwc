// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::foreign_toplevel::*;
use crate::rect::*;
use std::cmp::{max, min};
use std::ffi::CString;

const FALLBACK_WIDTH: i32 = 640;
const FALLBACK_HEIGHT: i32 = 480;
const MIN_WIDTH: i32 = 100;
const MIN_HEIGHT: i32 = 60;
const MIN_VISIBLE_PX: i32 = 16;

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

fn nearest_output_to_geom(geom: Rect) -> *mut Output {
    return unsafe { output_nearest_to(geom.x + geom.width / 2, geom.y + geom.height / 2) };
}

#[derive(Default)]
pub struct View {
    c_ptr: *mut CView,
    is_xwayland: bool,
    app_id: CString,
    title: CString,
    state: Box<ViewState>,
    foreign_toplevels: Vec<ForeignToplevel>,
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

    pub fn ensure_geom_onscreen(&self, geom: &mut Rect) {
        if rect_empty(*geom) {
            return;
        }
        let usable = unsafe { output_usable_area_in_layout_coords(self.state.output) };
        let margin = unsafe { ssd_get_margin(self.c_ptr) };
        let usable_minus_margin = rect_minus_margin(usable, margin);
        if rect_empty(usable_minus_margin) {
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
            *geom = rect_center(geom.width, geom.height, usable_minus_margin);
            rect_move_within(geom, usable_minus_margin);
        }
    }

    // Note: rel_to and keep_position are mutually exclusive
    pub fn compute_default_geom(
        &self,
        geom: &mut Rect,
        rel_to: Option<&Rect>,
        keep_position: bool,
    ) {
        let margin = unsafe { ssd_get_margin(self.c_ptr) };
        if rect_empty(*geom) {
            // Invalid size - just ensure top-left corner is non-negative
            geom.x = max(geom.x, margin.left);
            geom.y = max(geom.y, margin.top);
            return;
        }
        // Recompute output from reference (parent) geometry if specified
        let output = rel_to.map_or(self.state.output, |&r| nearest_output_to_geom(r));
        let usable = unsafe { output_usable_area_in_layout_coords(output) };
        let usable_minus_margin = rect_minus_margin(usable, margin);
        let rel_to_minus_margin = rel_to.map(|&r| rect_minus_margin(r, margin));
        if rect_empty(usable_minus_margin) {
            // Invalid output geometry - center to parent if possible,
            // then ensure top-left corner is non-negative
            if let Some(r) = rel_to_minus_margin {
                *geom = rect_center(geom.width, geom.height, r);
            }
            geom.x = max(geom.x, margin.left);
            geom.y = max(geom.y, margin.top);
            return;
        }
        // Limit size (including margins) to usable area
        geom.width = min(geom.width, usable_minus_margin.width);
        geom.height = min(geom.height, usable_minus_margin.height);
        // Center as requested
        if let Some(r) = rel_to_minus_margin {
            *geom = rect_center(geom.width, geom.height, r);
        } else if !keep_position {
            *geom = rect_center(geom.width, geom.height, usable_minus_margin);
        }
        // Finally, move within usable area
        rect_move_within(geom, usable_minus_margin);
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
        unsafe { view_notify_move_resize(self.c_ptr) };
    }

    pub fn set_initial_geom(&mut self, rel_to: Option<&Rect>, keep_position: bool) {
        if self.state.floating() {
            let mut geom = self.state.pending;
            self.compute_default_geom(&mut geom, rel_to, keep_position);
            self.move_resize(geom);
        } else {
            // An xwayland view should have a reasonable natural geometry.
            // For an xdg-shell view, it is allowed to be empty initially.
            if self.is_xwayland
                && (self.state.natural_geom.width < MIN_WIDTH
                    || self.state.natural_geom.height < MIN_HEIGHT)
            {
                self.set_fallback_natural_geom();
            }
        }
    }

    pub fn set_fallback_natural_geom(&mut self) {
        let mut natural = Rect {
            width: FALLBACK_WIDTH,
            height: FALLBACK_HEIGHT,
            ..self.state.natural_geom
        };
        self.compute_default_geom(
            &mut natural,
            /* rel_to */ None,
            /* keep_position */ false,
        );
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

    pub fn set_output(&mut self, output: *mut Output) {
        self.state.output = output;
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
