// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::foreign_toplevel::*;
use crate::rect::*;
use crate::ssd::*;
use crate::util::*;
use crate::view_geom::*;
use crate::view_impl::*;
use std::cmp::max;
use std::ffi::CString;
use std::ops::BitOrAssign;
use std::ptr::null_mut;

const FALLBACK_WIDTH: i32 = 640;
const FALLBACK_HEIGHT: i32 = 480;

// Returned to indicate a "post-processing" update is needed.
// Levels are cumulative: each level also implies the previous.
#[must_use]
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord)]
pub enum UpdateLevel {
    None,
    Cursor, // i.e. cursor_update_focus()
}

impl BitOrAssign for UpdateLevel {
    // unfortunately this counts as "using" self
    fn bitor_assign(&mut self, other: Self) {
        *self = max(*self, other);
    }
}

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
struct ViewData {
    app_id: CString,
    title: CString,
    ssd: Ssd,
    foreign_toplevels: Vec<ForeignToplevel>,
    icon_surfaces: Vec<CairoSurfacePtr>,
    icon_buffer: Option<WlrBufferPtr>,
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

    pub fn get_c_ptr(&self) -> *mut CView {
        self.c_ptr
    }

    pub fn get_state(&self) -> &ViewState {
        &self.state
    }

    pub fn get_root_id(&self) -> ViewId {
        self.v.get_root_id()
    }

    pub fn is_modal_dialog(&self) -> bool {
        self.v.is_modal_dialog()
    }

    pub fn get_size_hints(&self) -> ViewSizeHints {
        self.v.get_size_hints()
    }

    pub fn has_strut_partial(&self) -> bool {
        self.v.has_strut_partial()
    }

    pub fn set_app_id(&mut self, app_id: CString) {
        if self.d.app_id != app_id {
            self.d.app_id = app_id;
            self.state.app_id = self.d.app_id.as_ptr(); // for C interop
            for toplevel in &self.d.foreign_toplevels {
                toplevel.send_app_id(&self.d.app_id);
                toplevel.send_done();
            }
            self.update_icon();
        }
    }

    pub fn set_title(&mut self, title: CString) {
        if self.d.title != title {
            self.d.title = title;
            self.state.title = self.d.title.as_ptr(); // for C interop
            self.d.ssd.update_title();
            for toplevel in &self.d.foreign_toplevels {
                toplevel.send_title(&self.d.title);
                toplevel.send_done();
            }
        }
    }

    pub fn set_mapped(&mut self, was_shown: &mut bool) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        self.state.mapped = true;
        self.state.ever_mapped = true;
        self.state.focus_mode = self.v.get_focus_mode();
        if !self.state.minimized {
            unsafe { view_set_visible(self.c_ptr, true) };
            *was_shown = true;
            ul |= UpdateLevel::Cursor;
        }
        // Create SSD at map (if needed)
        ul |= self.update_ssd();
        return ul;
    }

    pub fn set_unmapped(&mut self, was_hidden: &mut bool) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        self.state.mapped = false;
        if !self.state.minimized {
            unsafe { view_set_visible(self.c_ptr, false) };
            *was_hidden = true;
            ul |= UpdateLevel::Cursor;
        }
        for resource in self.d.foreign_toplevels.drain(..) {
            resource.close();
        }
        return ul;
    }

    pub fn set_active(&mut self, active: bool) {
        if self.state.active != active {
            self.state.active = active;
            self.v.set_active(active);
            self.d.ssd.set_active(active);
            self.send_foreign_toplevel_state();
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

    pub fn set_minimized(&mut self, minimized: bool, visibility_changed: &mut bool) -> UpdateLevel {
        if self.state.minimized != minimized {
            self.state.minimized = minimized;
            self.v.set_minimized(minimized);
            self.send_foreign_toplevel_state();
            if self.state.mapped {
                unsafe { view_set_visible(self.c_ptr, !minimized) };
                *visibility_changed = true;
                return UpdateLevel::Cursor;
            }
        }
        return UpdateLevel::None;
    }

    pub fn set_pending_geom(&mut self, geom: Rect) {
        self.state.pending = geom;
    }

    pub fn move_resize(&mut self, geom: Rect) -> UpdateLevel {
        if rect_equals(self.state.pending, geom) {
            return UpdateLevel::None;
        }
        let mut commit_move = false;
        self.v.configure(geom, &mut commit_move);
        self.state.pending = geom;
        if self.state.floating() {
            // Moving a floating view also sets the output
            self.state.output = nearest_output_to_geom(self.state.pending);
        }
        if !self.d.in_layout_change {
            // User-initiated move/resize invalidates saved geometry
            self.d.saved_geom = Rect::default();
            self.d.lost_output = false;
        }
        if commit_move {
            return self.commit_move(geom.x, geom.y);
        }
        return UpdateLevel::None;
    }

    pub fn commit_move(&mut self, x: i32, y: i32) -> UpdateLevel {
        (self.state.current.x, self.state.current.y) = self.v.adjust_scene_pos(&self.state, x, y);
        unsafe { view_move_impl(self.c_ptr) };
        self.d.ssd.update_geom();
        if self.state.visible() {
            return UpdateLevel::Cursor;
        }
        return UpdateLevel::None;
    }

    pub fn commit_geom(&mut self, width: i32, height: i32, resize_edges: LabEdge) -> UpdateLevel {
        let (x, y) = compute_display_position(
            self.state.current,
            self.state.pending,
            width,
            height,
            resize_edges,
        );
        self.state.current.width = width;
        self.state.current.height = height;
        return self.commit_move(x, y);
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

    fn apply_natural_geom(&mut self) -> UpdateLevel {
        let mut natural = self.state.natural_geom;
        ensure_geom_onscreen(&*self.state, &mut natural);
        return self.move_resize(natural);
    }

    fn apply_special_geom(&mut self) -> UpdateLevel {
        let geom;
        if self.state.fullscreen {
            geom = unsafe { output_layout_coords(self.state.output) };
        } else if self.state.maximized != VIEW_AXIS_NONE {
            geom = compute_maximized_geom(&*self.state);
        } else if self.state.tiled != 0 {
            geom = compute_tiled_geom(&*self.state);
        } else {
            return UpdateLevel::None; // defensive
        }
        if rect_empty(geom) {
            return UpdateLevel::None;
        }
        return self.move_resize(geom);
    }

    pub fn set_output(&mut self, output: *mut Output) {
        self.state.output = output;
    }

    pub fn adjust_for_layout_change(&mut self) -> UpdateLevel {
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
        let ul = if !is_floating {
            self.apply_special_geom()
        } else if self.has_strut_partial() {
            // Do not move panels etc. out of their own reserved area
            UpdateLevel::None
        } else {
            // Restore saved geometry, ensuring view is on-screen
            let mut geom = self.d.saved_geom;
            ensure_geom_onscreen(&*self.state, &mut geom);
            self.move_resize(geom)
        };
        self.d.in_layout_change = false;
        return ul;
    }

    fn update_ssd(&mut self) -> UpdateLevel {
        if self.state.ssd_enabled && !self.state.fullscreen {
            let icon_buffer = self.get_icon_buffer();
            self.d.ssd.create(self.c_ptr, icon_buffer);
        } else {
            self.d.ssd.destroy();
        }
        if self.state.visible() {
            return UpdateLevel::Cursor;
        }
        return UpdateLevel::None;
    }

    pub fn enable_ssd(&mut self, enabled: bool) -> UpdateLevel {
        if self.state.ssd_enabled == enabled {
            return UpdateLevel::None;
        }
        self.state.ssd_enabled = enabled;
        let mut ul = UpdateLevel::None;
        if !self.state.fullscreen {
            if !self.state.floating() {
                ul |= self.apply_special_geom();
            }
            if self.state.mapped {
                ul |= self.update_ssd();
            }
        }
        return ul;
    }

    pub fn destroy_ssd(&mut self) {
        self.d.ssd.destroy();
    }

    // Returns >= UpdateLevel::Cursor if visible and state changed
    pub fn fullscreen(&mut self, fullscreen: bool) -> UpdateLevel {
        if self.state.fullscreen == fullscreen {
            return UpdateLevel::None;
        }
        if fullscreen {
            self.store_natural_geom();
        }
        self.set_fullscreen(fullscreen);
        let mut ul = UpdateLevel::None;
        if self.state.floating() {
            ul |= self.apply_natural_geom();
        } else {
            ul |= self.apply_special_geom();
        }
        if self.state.mapped {
            ul |= self.update_ssd();
        }
        return ul;
    }

    pub fn maximize(&mut self, axis: ViewAxis, is_moving: bool) -> UpdateLevel {
        if self.state.maximized == axis {
            return UpdateLevel::None;
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
            return self.apply_natural_geom();
        } else {
            return self.apply_special_geom();
        }
    }

    pub fn tile(&mut self, edge: LabEdge, is_moving: bool) -> UpdateLevel {
        if self.state.tiled == edge {
            return UpdateLevel::None;
        }
        // In snap-to-tile case, natural geometry was already stored
        if !is_moving {
            self.store_natural_geom();
        }
        self.set_tiled(edge);
        if self.state.floating() {
            return self.apply_natural_geom();
        } else {
            return self.apply_special_geom();
        }
    }

    pub fn raise(&self) {
        unsafe { view_raise_impl(self.c_ptr) };
    }

    // Returns true if focus was (immediately) changed
    pub fn focus(&self) -> bool {
        if self.state.mapped {
            if self.state.focus_mode == VIEW_FOCUS_MODE_ALWAYS {
                return self.v.focus();
            } else if self.state.focus_mode == VIEW_FOCUS_MODE_UNLIKELY
                || self.state.focus_mode == VIEW_FOCUS_MODE_LIKELY
            {
                self.v.offer_focus();
            }
        }
        return false;
    }

    pub fn refocus(&self) {
        self.v.focus();
    }

    pub fn set_inhibits_keybinds(&mut self, inhibits_keybinds: bool) {
        self.state.inhibits_keybinds = inhibits_keybinds;
    }

    pub fn close(&self) {
        self.v.close();
    }

    pub fn add_foreign_toplevel(&mut self, client: *mut WlResource, id: ViewId) {
        let toplevel = ForeignToplevel::new(client, id);
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

    pub fn add_icon_surface(&mut self, surface: CairoSurfacePtr) {
        self.d.icon_surfaces.push(surface);
    }

    fn get_best_icon_surface(&self, desired_size: i32) -> *mut CairoSurface {
        let mut best = null_mut();
        let mut best_diff = i32::MIN;
        for surf in &self.d.icon_surfaces {
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
        self.d.icon_surfaces.clear();
    }

    pub fn get_icon_buffer(&mut self) -> *mut WlrBuffer {
        if let Some(buf) = &self.d.icon_buffer {
            return buf.buffer;
        }
        let icon_size = unsafe { ssd_get_icon_buffer_size() };
        let icon_surface = self.get_best_icon_surface(icon_size);
        let icon_buffer = unsafe { scaled_icon_buffer_load(self.d.app_id.as_ptr(), icon_surface) };
        if !icon_buffer.is_null() {
            self.d.icon_buffer = Some(WlrBufferPtr::new(icon_buffer));
            return icon_buffer;
        }
        return null_mut();
    }

    pub fn update_icon(&mut self) {
        self.d.icon_buffer = None;
        let icon_buffer = self.get_icon_buffer();
        self.d.ssd.update_icon(icon_buffer);
    }

    pub fn reload_ssd(&mut self) -> UpdateLevel {
        self.d.icon_buffer = None;
        self.d.ssd.destroy();
        if self.state.mapped {
            return self.update_ssd();
        }
        return UpdateLevel::None;
    }
}
