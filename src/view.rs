// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::foreign_toplevel::*;
use crate::rect::*;
use crate::util::*;
use std::cmp::{max, min};
use std::ffi::CString;
use std::ptr::null_mut;

const FALLBACK_WIDTH: i32 = 640;
const FALLBACK_HEIGHT: i32 = 480;
const MIN_WIDTH: i32 = 100;
const MIN_HEIGHT: i32 = 60;
const MIN_VISIBLE_PX: i32 = 16;

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

fn substitute_nonzero(a: &mut i32, b: &mut i32) {
    if *a == 0 {
        *a = *b;
    } else if *b == 0 {
        *b = *a;
    }
}

fn round_to_increment(val: i32, base: i32, inc: i32) -> i32 {
    if base < 0 || inc <= 0 {
        return val;
    }
    return base + (val - base + inc / 2) / inc * inc;
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

    fn get_size_hints(&self) -> ViewSizeHints {
        if self.is_xwayland {
            unsafe { xwayland_view_get_size_hints(self.c_ptr) }
        } else {
            unsafe { xdg_toplevel_view_get_size_hints(self.c_ptr) }
        }
    }

    pub fn adjust_size(&self, width: &mut i32, height: &mut i32) {
        let mut hints = self.get_size_hints();
        // "If a base size is not provided, the minimum size is to be
        // used in its place and vice versa." (ICCCM 4.1.2.3)
        substitute_nonzero(&mut hints.min_width, &mut hints.base_width);
        substitute_nonzero(&mut hints.min_height, &mut hints.base_height);
        // Snap width/height to requested size increments (if any)
        *width = round_to_increment(*width, hints.base_width, hints.width_inc);
        *height = round_to_increment(*height, hints.base_height, hints.height_inc);
        // If minimum width/height was not set, then use default
        if hints.min_width < 1 {
            hints.min_width = MIN_WIDTH;
        }
        if hints.min_height < 1 {
            hints.min_height = MIN_HEIGHT;
        }
        *width = max(*width, hints.min_width);
        *height = max(*height, hints.min_height);
    }

    pub fn has_strut_partial(&self) -> bool {
        if self.is_xwayland {
            unsafe { xwayland_view_has_strut_partial(self.c_ptr) }
        } else {
            false
        }
    }

    // Returns CView pointer to pass to view_notify_icon_change()
    pub fn set_app_id(&mut self, app_id: CString) -> *mut CView {
        if self.app_id != app_id {
            self.app_id = app_id;
            self.state.app_id = self.app_id.as_ptr(); // for C interop
            for toplevel in &self.foreign_toplevels {
                toplevel.send_app_id(&self.app_id);
                toplevel.send_done();
            }
            return self.c_ptr;
        }
        return null_mut();
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

    pub fn set_mapped(&mut self, focus_mode: ViewFocusMode, was_shown: &mut bool) {
        self.state.mapped = true;
        self.state.ever_mapped = true;
        self.state.focus_mode = focus_mode;
        if !self.state.minimized {
            unsafe { view_set_visible(self.c_ptr, true) };
            *was_shown = true;
        }
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
            unsafe { view_notify_active(self.c_ptr) };
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

    fn ensure_geom_onscreen(&self, geom: &mut Rect) {
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
        if !self.in_layout_change {
            // User-initiated move/resize invalidates saved geometry
            self.saved_geom = Rect::default();
            self.lost_output = false;
        }
    }

    pub fn commit_size(&mut self, width: i32, height: i32, resize_edges: LabEdge) {
        let cur = self.state.current;
        let pend = self.state.pending;
        // Anchor right edge if resizing via left edge
        // (or if recently resizing, detected via heuristic)
        let resizing_left = (resize_edges & LAB_EDGE_LEFT) != 0;
        let x = if resizing_left || (cur.x != pend.x && cur.x + cur.width == pend.x + pend.width) {
            pend.x + pend.width - width
        } else {
            pend.x
        };
        // Anchor bottom edge if resizing via top edge
        // (or if recently resizing, detected via heuristic)
        let resizing_top = (resize_edges & LAB_EDGE_TOP) != 0;
        let y = if resizing_top || (cur.y != pend.y && cur.y + cur.height == pend.y + pend.height) {
            pend.y + pend.height - height
        } else {
            pend.y
        };
        self.state.current = Rect {
            x: x,
            y: y,
            width: width,
            height: height,
        };
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

    fn set_fallback_natural_geom(&mut self) {
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

    fn apply_natural_geom(&mut self) {
        let mut natural = self.state.natural_geom;
        self.ensure_geom_onscreen(&mut natural);
        self.move_resize(natural);
    }

    fn apply_fullscreen_geom(&mut self) {
        let geom = unsafe { output_layout_coords(self.state.output) };
        if !rect_empty(geom) {
            self.move_resize(geom);
        }
    }

    fn apply_maximized_geom(&mut self) {
        let usable = unsafe { output_usable_area_in_layout_coords(self.state.output) };
        let margin = unsafe { ssd_get_margin(self.c_ptr) };
        let mut geom = rect_minus_margin(usable, margin);
        // If one axis (horizontal or vertical) is unmaximized, it should
        // use the natural geometry (first ensuring it is on-screen)
        if self.state.maximized != VIEW_AXIS_BOTH {
            let mut natural = self.state.natural_geom;
            self.ensure_geom_onscreen(&mut natural);
            if self.state.maximized == VIEW_AXIS_VERTICAL {
                geom.x = natural.x;
                geom.width = natural.width;
            } else if self.state.maximized == VIEW_AXIS_HORIZONTAL {
                geom.y = natural.y;
                geom.height = natural.height;
            }
        }
        if !rect_empty(geom) {
            self.move_resize(geom);
        }
    }

    fn apply_tiled_geom(&mut self) {
        let usable = unsafe { output_usable_area_in_layout_coords(self.state.output) };
        let margin = unsafe { ssd_get_margin(self.c_ptr) };
        let (mut x1, mut x2) = (0, usable.width);
        let (mut y1, mut y2) = (0, usable.height);
        if (self.state.tiled & LAB_EDGE_RIGHT) != 0 {
            x1 = usable.width / 2;
        }
        if (self.state.tiled & LAB_EDGE_LEFT) != 0 {
            x2 = usable.width / 2;
        }
        if (self.state.tiled & LAB_EDGE_BOTTOM) != 0 {
            y1 = usable.height / 2;
        }
        if (self.state.tiled & LAB_EDGE_TOP) != 0 {
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

    pub fn apply_special_geom(&mut self) {
        if self.state.fullscreen {
            self.apply_fullscreen_geom();
        } else if self.state.maximized != VIEW_AXIS_NONE {
            self.apply_maximized_geom();
        } else if self.state.tiled != 0 {
            self.apply_tiled_geom();
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
            self.ensure_geom_onscreen(&mut geom);
            self.move_resize(geom);
        }
        self.in_layout_change = false;
    }

    // Returns CView pointer to pass to view_notify_fullscreen()
    pub fn fullscreen(&mut self, fullscreen: bool) -> *mut CView {
        if self.state.fullscreen == fullscreen {
            return null_mut();
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
        return self.c_ptr;
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
        if self.state.inhibits_keybinds != inhibits_keybinds {
            self.state.inhibits_keybinds = inhibits_keybinds;
            unsafe { view_notify_inhibits_keybinds(self.c_ptr) };
        }
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

    pub fn get_icon_buffer(&mut self, icon_size: i32, scale: f32) -> *mut WlrBuffer {
        if let Some(buf) = &self.icon_buffer {
            return buf.buffer;
        }
        let icon_surface = self.get_best_icon_surface((icon_size as f32 * scale) as i32);
        let icon_buffer = unsafe {
            scaled_icon_buffer_load(self.app_id.as_ptr(), icon_surface, icon_size, scale)
        };
        if !icon_buffer.is_null() {
            self.icon_buffer = Some(WlrBufferPtr::new(icon_buffer));
            return icon_buffer;
        }
        return null_mut();
    }

    pub fn drop_icon_buffer(&mut self) {
        self.icon_buffer = None;
    }
}
