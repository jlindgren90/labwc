// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::foreign_toplevel::*;
use crate::rect::*;
use crate::ssd::*;
use crate::util::*;
use crate::view_geom::*;
use std::ffi::CString;
use std::ptr::null_mut;

const FALLBACK_WIDTH: i32 = 640;
const FALLBACK_HEIGHT: i32 = 480;

impl ViewState {
    pub fn visible(&self) -> bool {
        self.mapped && !self.minimized
    }

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
    ssd: Ssd,
    foreign_toplevels: Vec<ForeignToplevel>,
    icon_surfaces: Vec<CairoSurfacePtr>,
    icon_buffer: Option<WlrBufferPtr>,
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

    pub fn get_c_ptr(&self) -> *mut CView {
        self.c_ptr
    }

    pub fn get_state(&self) -> &ViewState {
        &self.state
    }

    pub fn get_root_id(&self) -> ViewId {
        if self.is_xwayland {
            unsafe { xwayland_view_get_root_id(self.c_ptr) }
        } else {
            unsafe { xdg_toplevel_view_get_root_id(self.c_ptr) }
        }
    }

    pub fn is_modal_dialog(&self) -> bool {
        if self.is_xwayland {
            unsafe { xwayland_view_is_modal_dialog(self.c_ptr) }
        } else {
            unsafe { xdg_toplevel_view_is_modal_dialog(self.c_ptr) }
        }
    }

    pub fn get_size_hints(&self) -> ViewSizeHints {
        if self.is_xwayland {
            unsafe { xwayland_view_get_size_hints(self.c_ptr) }
        } else {
            unsafe { xdg_toplevel_view_get_size_hints(self.c_ptr) }
        }
    }

    pub fn has_strut_partial(&self) -> bool {
        if self.is_xwayland {
            unsafe { xwayland_view_has_strut_partial(self.c_ptr) }
        } else {
            false
        }
    }

    pub fn set_app_id(&mut self, app_id: CString) {
        if self.app_id != app_id {
            self.app_id = app_id;
            self.state.app_id = self.app_id.as_ptr(); // for C interop
            for toplevel in &self.foreign_toplevels {
                toplevel.send_app_id(&self.app_id);
                toplevel.send_done();
            }
            self.update_icon();
        }
    }

    pub fn set_title(&mut self, title: CString) {
        if self.title != title {
            self.title = title;
            self.state.title = self.title.as_ptr(); // for C interop
            self.ssd.update_title();
            for toplevel in &self.foreign_toplevels {
                toplevel.send_title(&self.title);
                toplevel.send_done();
            }
        }
    }

    pub fn set_mapped(&mut self, focus_mode: ViewFocusMode, was_shown: &mut bool) {
        self.state.mapped = true;
        self.state.ever_mapped = true;
        self.state.focus_mode = focus_mode;
        if !self.state.minimized {
            unsafe { view_set_visible(self.c_ptr, true) };
            *was_shown = true;
        }
        // Create SSD at map (if needed)
        self.update_ssd();
    }

    pub fn set_unmapped(&mut self, was_hidden: &mut bool) {
        self.state.mapped = false;
        if !self.state.minimized {
            unsafe { view_set_visible(self.c_ptr, false) };
            *was_hidden = true;
        }
        for resource in self.foreign_toplevels.drain(..) {
            resource.close();
        }
    }

    pub fn set_active(&mut self, active: bool) {
        if self.state.active != active {
            self.state.active = active;
            if self.is_xwayland {
                unsafe { xwayland_view_set_active(self.c_ptr, active) };
            } else {
                unsafe { xdg_toplevel_view_set_active(self.c_ptr, active) };
            }
            self.ssd.set_active(active);
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

    pub fn set_minimized(&mut self, minimized: bool, visibility_changed: &mut bool) {
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

    pub fn set_pending_geom(&mut self, geom: Rect) {
        self.state.pending = geom;
    }

    pub fn move_resize(&mut self, geom: Rect) {
        if rect_equals(self.state.pending, geom) {
            return;
        }
        let mut commit_move = false;
        if self.is_xwayland {
            unsafe { xwayland_view_configure(self.c_ptr, geom, &mut commit_move) };
        } else {
            unsafe { xdg_toplevel_view_configure(self.c_ptr, geom, &mut commit_move) };
        }
        self.state.pending = geom;
        if self.state.floating() {
            // Moving a floating view also sets the output
            self.state.output = nearest_output_to_geom(self.state.pending);
        }
        if !self.in_layout_change {
            // User-initiated move/resize invalidates saved geometry
            self.saved_geom = Rect::default();
            self.lost_output = false;
        }
        if commit_move {
            self.commit_move(geom.x, geom.y);
        }
    }

    fn center_fullscreen(&mut self) {
        let output_geom = unsafe { output_layout_coords(self.state.output) };
        if rect_empty(output_geom) {
            unsafe { xdg_toplevel_view_disable_fullscreen_bg(self.c_ptr) };
            return;
        }
        self.state.current = rect_center(
            self.state.current.width,
            self.state.current.height,
            output_geom,
        );
        rect_move_within(&mut self.state.current, output_geom);
        if self.state.current.width < output_geom.width
            || self.state.current.width < output_geom.height
        {
            unsafe { xdg_toplevel_view_enable_fullscreen_bg(self.c_ptr, output_geom) };
        } else {
            unsafe { xdg_toplevel_view_disable_fullscreen_bg(self.c_ptr) };
        }
    }

    pub fn commit_move(&mut self, x: i32, y: i32) {
        self.state.current.x = x;
        self.state.current.y = y;
        // Only xdg-shell views are centered when fullscreen
        if self.state.fullscreen && !self.is_xwayland {
            self.center_fullscreen();
        }
        unsafe { view_move_impl(self.c_ptr) };
        self.ssd.update_geom();
        unsafe { cursor_update_focus() };
    }

    pub fn commit_geom(&mut self, width: i32, height: i32, resize_edges: LabEdge) {
        let (x, y) = compute_display_position(
            self.state.current,
            self.state.pending,
            width,
            height,
            resize_edges,
        );
        self.state.current.width = width;
        self.state.current.height = height;
        self.commit_move(x, y);
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

    fn set_fallback_natural_geom(&mut self) {
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

    fn apply_special_geom(&mut self) {
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
        } else if self.has_strut_partial() {
            // Do not move panels etc. out of their own reserved area
        } else {
            // Restore saved geometry, ensuring view is on-screen
            let mut geom = self.saved_geom;
            ensure_geom_onscreen(&*self.state, &mut geom);
            self.move_resize(geom);
        }
        self.in_layout_change = false;
    }

    fn update_ssd(&mut self) {
        if self.state.ssd_enabled && !self.state.fullscreen {
            let icon_buffer = self.get_icon_buffer();
            self.ssd.create(self.c_ptr, icon_buffer);
        } else {
            self.ssd.destroy();
        }
    }

    pub fn enable_ssd(&mut self, enabled: bool) {
        if self.state.ssd_enabled == enabled {
            return;
        }
        self.state.ssd_enabled = enabled;
        if !self.state.fullscreen {
            if !self.state.floating() {
                self.apply_special_geom();
            }
            if self.state.mapped {
                self.update_ssd();
                unsafe { cursor_update_focus() };
            }
        }
    }

    pub fn destroy_ssd(&mut self) {
        self.ssd.destroy();
    }

    // Returns true if mapped and state changed
    pub fn fullscreen(&mut self, fullscreen: bool) -> bool {
        if self.state.fullscreen == fullscreen {
            return false;
        }
        if fullscreen {
            self.store_natural_geom();
        }
        self.set_fullscreen(fullscreen);
        if self.state.floating() {
            self.apply_natural_geom();
        } else {
            self.apply_special_geom();
        }
        if self.state.mapped {
            self.update_ssd();
            return true;
        }
        return false;
    }

    pub fn maximize(&mut self, axis: ViewAxis, is_moving: bool) {
        if self.state.maximized == axis {
            return;
        }
        // In snap-to-maximize case, natural geometry was already stored
        if !is_moving {
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

    pub fn tile(&mut self, edge: LabEdge, is_moving: bool) {
        if self.state.tiled == edge {
            return;
        }
        // In snap-to-tile case, natural geometry was already stored
        if !is_moving {
            self.store_natural_geom();
        }
        self.set_tiled(edge);
        if self.state.floating() {
            self.apply_natural_geom();
        } else {
            self.apply_special_geom();
        }
    }

    pub fn raise(&self) {
        unsafe { view_raise_impl(self.c_ptr) };
    }

    // Returns true if focus was (immediately) changed
    pub fn focus(&self) -> bool {
        if self.state.mapped {
            if self.state.focus_mode == VIEW_FOCUS_MODE_ALWAYS {
                return unsafe { view_focus_impl(self.c_ptr) };
            } else if self.is_xwayland
                && (self.state.focus_mode == VIEW_FOCUS_MODE_UNLIKELY
                    || self.state.focus_mode == VIEW_FOCUS_MODE_LIKELY)
            {
                unsafe { xwayland_view_offer_focus(self.c_ptr) };
            }
        }
        return false;
    }

    pub fn set_inhibits_keybinds(&mut self, inhibits_keybinds: bool) {
        self.state.inhibits_keybinds = inhibits_keybinds;
    }

    pub fn close(&self) {
        if self.is_xwayland {
            unsafe { xwayland_view_close(self.c_ptr) };
        } else {
            unsafe { xdg_toplevel_view_close(self.c_ptr) };
        }
    }

    pub fn add_foreign_toplevel(&mut self, client: *mut WlResource, id: ViewId) {
        let toplevel = ForeignToplevel::new(client, id);
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

    pub fn add_icon_surface(&mut self, surface: CairoSurfacePtr) {
        self.icon_surfaces.push(surface);
    }

    fn get_best_icon_surface(&self, desired_size: i32) -> *mut CairoSurface {
        let mut best = null_mut();
        let mut best_diff = i32::MIN;
        for surf in &self.icon_surfaces {
            let width = unsafe { cairo_image_surface_get_width(surf.surface) };
            let height = unsafe { cairo_image_surface_get_height(surf.surface) };
            let diff = ((width + height) / 2) - desired_size;
            // Current is better if:
            // - previous was too small and current is bigger, or
            // - current is big enough and smaller than previous
            if (best_diff < 0 && diff > best_diff) || (diff >= 0 && diff < best_diff) {
                best = surf.surface;
                best_diff = diff;
            }
        }
        return best;
    }

    pub fn clear_icon_surfaces(&mut self) {
        self.icon_surfaces.clear();
    }

    pub fn get_icon_buffer(&mut self) -> *mut WlrBuffer {
        if let Some(buf) = &self.icon_buffer {
            return buf.buffer;
        }
        let icon_size = unsafe { ssd_get_icon_buffer_size() };
        let icon_surface = self.get_best_icon_surface(icon_size);
        let icon_buffer = unsafe { scaled_icon_buffer_load(self.app_id.as_ptr(), icon_surface) };
        if !icon_buffer.is_null() {
            self.icon_buffer = Some(WlrBufferPtr::new(icon_buffer));
            return icon_buffer;
        }
        return null_mut();
    }

    pub fn update_icon(&mut self) {
        self.icon_buffer = None;
        let icon_buffer = self.get_icon_buffer();
        self.ssd.update_icon(icon_buffer);
    }

    pub fn reload_ssd(&mut self) {
        self.icon_buffer = None;
        self.ssd.destroy();
        if self.state.mapped {
            self.update_ssd();
        }
    }
}
