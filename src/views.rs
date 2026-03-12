// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::view::*;
use crate::view_geom::*;
use crate::view_grab::*;
use std::collections::{BTreeMap, HashSet};
use std::ptr::null_mut;

#[derive(Default)]
pub struct Views {
    by_id: BTreeMap<ViewId, View>, // in creation order
    max_used_id: ViewId,
    order: Vec<ViewId>, // from back to front
    active_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
    grabbed_id: ViewId, // set at mouse button press
    moving_id: ViewId,  // set once cursor actually moves
    resizing_id: ViewId,
    grab: ViewGrab,
    cycle_list: Vec<ViewId>, // TODO: move elsewhere?
}

impl Views {
    pub fn add(&mut self, c_ptr: *mut CView, is_xwayland: bool) -> ViewId {
        self.max_used_id += 1;
        let id = self.max_used_id;
        self.by_id.insert(id, View::new(id, c_ptr, is_xwayland));
        self.order.push(id);
        return id;
    }

    pub fn remove(&mut self, id: ViewId) {
        self.by_id.remove(&id);
        self.order.retain(|&i| i != id);
    }

    pub fn get_view(&self, id: ViewId) -> Option<&View> {
        self.by_id.get(&id)
    }

    pub fn get_view_mut(&mut self, id: ViewId) -> Option<&mut View> {
        self.by_id.get_mut(&id)
    }

    fn get_root_of(&self, id: ViewId) -> ViewId {
        self.by_id.get(&id).map_or(0, View::get_root_id)
    }

    fn get_modal_dialog(&self, id: ViewId) -> Option<ViewId> {
        if let Some(view) = self.by_id.get(&id) {
            // Check if view itself is a modal dialog
            if view.get_state().mapped && view.is_modal_dialog() {
                return Some(id);
            }
            // Check child/sibling views (in reverse stacking order)
            let root = view.get_root_id();
            for &i in self.order.iter().rev().filter(|&&i| i != id && i != root) {
                if let Some(v) = self.by_id.get(&i)
                    && v.get_state().mapped
                    && v.get_root_id() == root
                    && v.is_modal_dialog()
                {
                    return Some(i);
                }
            }
        }
        return None;
    }

    pub fn map(&mut self, id: ViewId) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        if let Some(view) = self.by_id.get_mut(&id) {
            let mut was_shown = false;
            ul |= view.map(&mut was_shown);
            // Only focusable views should be shown in taskbars etc.
            if view.get_state().focusable() {
                for &client in &self.foreign_toplevel_clients {
                    view.add_foreign_toplevel(client, id);
                }
            }
            if was_shown {
                ul |= self.focus(id, /* raise */ true, /* force_restack */ true);
            }
        }
        return ul;
    }

    pub fn unmap(&mut self, id: ViewId) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        if let Some(view) = self.by_id.get_mut(&id) {
            let mut was_hidden = false;
            ul |= view.unmap(&mut was_hidden);
            if was_hidden {
                self.update_top_layer_visibility();
                ul |= self.reset_grab_for(Some(id));
                if !self.is_active_visible() {
                    ul |= self.focus_topmost();
                }
            }
        }
        return ul;
    }

    pub fn get_active(&self) -> ViewId {
        self.active_id
    }

    pub fn is_active_visible(&self) -> bool {
        let active = self.by_id.get(&self.active_id);
        return active.map_or(false, |a| a.get_state().visible());
    }

    pub fn set_active(&mut self, id: ViewId) {
        if id != self.active_id {
            let prev_id = self.active_id;
            self.active_id = id;
            if let Some(prev) = self.by_id.get_mut(&prev_id) {
                prev.set_active(false);
            }
            if let Some(view) = self.by_id.get_mut(&id) {
                view.set_active(true);
            }
        }
    }

    pub fn commit_geom(&mut self, id: ViewId, width: i32, height: i32) -> UpdateLevel {
        let mut resize_edges = LAB_EDGE_NONE;
        if id == self.resizing_id {
            resize_edges = self.grab.get_resize_edges();
        }
        if let Some(view) = self.by_id.get_mut(&id) {
            return view.commit_geom(width, height, resize_edges);
        } else {
            return UpdateLevel::None;
        }
    }

    // Called from xdg-shell commit handler
    pub fn set_initial_commit_size(
        &mut self,
        id: ViewId,
        width: i32,
        height: i32,
        cursor_x: i32,
        cursor_y: i32,
    ) {
        let (output, parent_geom);
        if let Some(view) = self.by_id.get(&id)
            && let Some(parent) = self.by_id.get(&view.get_parent())
        {
            output = parent.get_state().output;
            parent_geom = parent.get_state().pending;
        } else {
            output = unsafe { output_nearest_to_cursor() };
            parent_geom = Rect::default();
        }
        if let Some(view) = self.by_id.get_mut(&id)
            && view.get_state().floating()
        {
            view.set_output(output);
            let state = view.get_state();
            let mut geom = state.pending;
            (geom.width, geom.height) = (width, height);
            if id == self.moving_id {
                self.grab.adjust_move_origin(width, height);
                (geom.x, geom.y) = self.grab.compute_move_position(cursor_x, cursor_y);
            } else {
                compute_default_geom(state, &mut geom, parent_geom, false);
            }
            // Update pending geometry directly unless size was changed
            if geom.width == width && geom.height == height {
                view.set_pending_geom(geom);
            } else {
                // Ignore UpdateLevel due to being inside commit handler.
                // Updates are handled by commit_move() later if needed.
                _ = view.move_resize(geom);
            }
        }
    }

    pub fn on_output_destroy(&mut self, output: *mut Output) {
        for v in self.by_id.values_mut() {
            if v.get_state().output == output {
                v.set_output(null_mut());
            }
        }
    }

    // Assuming any caller already sets UpdateLevel::Cursor
    fn update_top_layer_visibility(&self) {
        unsafe { top_layer_show_all() };
        let mut outputs_seen = HashSet::new();
        for v in self.order.iter().rev().filter_map(|i| self.by_id.get(i)) {
            let state = v.get_state();
            if state.visible()
                && unsafe { output_is_usable(state.output) }
                && !outputs_seen.contains(&state.output)
            {
                // Hide top layer if topmost view is fullscreen
                if state.fullscreen {
                    unsafe { top_layer_hide_on_output(state.output) };
                }
                outputs_seen.insert(state.output);
            }
        }
    }

    pub fn adjust_for_layout_change(&mut self) -> UpdateLevel {
        let mut ul = UpdateLevel::Cursor;
        for v in self.by_id.values_mut() {
            ul |= v.adjust_for_layout_change();
        }
        self.update_top_layer_visibility();
        return ul;
    }

    pub fn adjust_usable_area(&self, output: *mut Output) {
        for v in self.by_id.values() {
            if v.get_state().mapped
                && let Some(strut) = v.get_strut_partial()
            {
                unsafe { output_adjust_usable_area_for_strut_partial(output, strut) };
            }
        }
    }

    pub fn fullscreen(&mut self, id: ViewId, fullscreen: bool, output: *mut Output) -> UpdateLevel {
        let Some(view) = self.by_id.get_mut(&id) else {
            return UpdateLevel::None;
        };
        let mut ul = view.fullscreen(fullscreen, output);
        if ul != UpdateLevel::None {
            // Entering/leaving fullscreen ends any interactive move/resize
            ul |= self.reset_grab_for(Some(id));
            self.update_top_layer_visibility();
        }
        return ul;
    }

    pub fn maximize(&mut self, id: ViewId, axis: ViewAxis) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        if let Some(view) = self.by_id.get_mut(&id)
            && view.get_state().maximized != axis
        {
            ul |= view.maximize(axis, id == self.moving_id, null_mut());
            // Maximizing/unmaximizing ends any interactive move/resize
            ul |= self.reset_grab_for(Some(id));
        }
        return ul;
    }

    pub fn tile(&mut self, id: ViewId, edge: LabEdge) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        if let Some(view) = self.by_id.get_mut(&id)
            && view.get_state().tiled != edge
        {
            ul |= view.tile(edge, id == self.moving_id, null_mut());
            // Tiling/untiling ends any interactive move/resize
            ul |= self.reset_grab_for(Some(id));
        }
        return ul;
    }

    // Returns true if visibility of any view changed
    pub fn minimize(&mut self, id: ViewId, minimized: bool) -> (bool, UpdateLevel) {
        let mut visibility_changed = false;
        let mut ul = UpdateLevel::None;
        if let Some(view) = self.by_id.get(&id)
            && view.get_state().minimized != minimized
        {
            // Minimize/unminimize all related views together
            let root = view.get_root_id();
            let mut reset_grab = false;
            for (&i, v) in &mut self.by_id {
                if v.get_root_id() == root {
                    ul |= v.set_minimized(minimized, &mut visibility_changed);
                    reset_grab |= i == self.grabbed_id;
                }
            }
            // Minimize ends any interactive move/resize
            if reset_grab {
                ul |= self.reset_grab_for(None);
            }
        }
        if visibility_changed {
            if !minimized {
                ul |= self.focus(id, /* raise */ true, /* force_restack */ true);
            } else if !self.is_active_visible() {
                ul |= self.focus_topmost();
                self.update_top_layer_visibility();
            }
        }
        return (visibility_changed, ul);
    }

    pub fn raise(&mut self, id: ViewId, force_restack: bool) -> UpdateLevel {
        // Check if view or a sub-view is already in front
        if !force_restack
            && let Some(&front) = self.order.last()
            && (id == front || self.get_root_of(front) == id)
        {
            return UpdateLevel::None;
        }
        let Some(view) = self.by_id.get(&id) else {
            return UpdateLevel::None;
        };
        let mut ids_to_raise = Vec::new();
        // Raise root parent view first
        let root = view.get_root_id();
        ids_to_raise.push(root);
        // Then other sub-views (in current stacking order)
        for &i in &self.order {
            if i != id && i != root && self.get_root_of(i) == root {
                ids_to_raise.push(i);
            }
        }
        // And finally specified view (if not root)
        if id != root {
            ids_to_raise.push(id);
        }
        for v in ids_to_raise.iter().filter_map(|i| self.by_id.get(i)) {
            v.raise();
        }
        self.order.retain(|i| !ids_to_raise.contains(i));
        self.order.append(&mut ids_to_raise);
        // Keep always-on-top views on top by raising again
        for v in self.order.iter().filter_map(|i| self.by_id.get(i)) {
            if v.get_state().always_on_top {
                v.raise();
            }
        }
        self.update_top_layer_visibility();
        return UpdateLevel::Cursor;
    }

    pub fn set_always_on_top(&mut self, id: ViewId, always_on_top: bool) -> UpdateLevel {
        if let Some(view) = self.by_id.get_mut(&id)
            && view.get_state().always_on_top != always_on_top
        {
            view.set_always_on_top(always_on_top);
            return self.raise(id, /* force_restack */ true);
        }
        return UpdateLevel::None;
    }

    pub fn focus(&mut self, id: ViewId, raise: bool, force_restack: bool) -> UpdateLevel {
        // Focus a modal dialog rather than its parent view
        let id_to_focus = self.get_modal_dialog(id).unwrap_or(id);
        let ul = if raise {
            self.raise(id_to_focus, force_restack)
        } else {
            UpdateLevel::None
        };
        if let Some(view) = self.by_id.get_mut(&id_to_focus)
            && view.focus()
        {
            self.set_active(id_to_focus);
        }
        return ul;
    }

    pub fn focus_topmost(&mut self) -> UpdateLevel {
        for &i in self.order.iter().rev() {
            if let Some(state) = self.by_id.get(&i).map(View::get_state)
                && state.focusable()
                && !state.minimized
            {
                return self.focus(i, /* raise */ false, /* force_restack */ false);
            }
        }
        if unsafe { seat_focus_surface_no_notify(null_mut()) } {
            self.set_active(0);
        }
        return UpdateLevel::None;
    }

    // Called after closing XWayland override-redirect window. Updates
    // compositor-side focus only. (Override-redirect windows are never
    // properly "focused" in X11 terms, using keyboard grabs instead.)
    pub fn refocus_active(&self) {
        if let Some(active) = self.by_id.get(&self.active_id) {
            active.refocus();
        }
    }

    pub fn add_foreign_toplevel_client(&mut self, client: *mut WlResource) {
        self.foreign_toplevel_clients.push(client);
        for (&id, view) in &mut self.by_id {
            if view.get_state().focusable() {
                view.add_foreign_toplevel(client, id);
            }
        }
    }

    pub fn remove_foreign_toplevel_client(&mut self, client: *mut WlResource) {
        self.foreign_toplevel_clients.retain(|&c| c != client);
    }

    pub fn reload_ssds(&mut self) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        for view in self.by_id.values_mut() {
            ul |= view.reload_ssd();
        }
        return ul;
    }

    pub fn set_grab_context(&mut self, id: ViewId, cursor_x: i32, cursor_y: i32, edges: LabEdge) {
        if self.grabbed_id == 0
            && let Some(view) = self.by_id.get_mut(&id)
        {
            self.grabbed_id = id;
            self.grab.set_context(view, cursor_x, cursor_y, edges);
        }
    }

    pub fn start_move(&mut self, id: ViewId) -> bool {
        if id == self.grabbed_id
            && let Some(view) = self.by_id.get_mut(&id)
            && self.grab.start_move(view)
        {
            self.moving_id = id;
            return true;
        }
        return false;
    }

    pub fn continue_move(&mut self, cursor_x: i32, cursor_y: i32) -> UpdateLevel {
        if let Some(view) = self.by_id.get_mut(&self.moving_id) {
            return self.grab.continue_move(view, cursor_x, cursor_y);
        }
        return UpdateLevel::None;
    }

    pub fn start_resize(&mut self, id: ViewId, edges: LabEdge) -> bool {
        if id == self.grabbed_id
            && let Some(view) = self.by_id.get_mut(&id)
            && self.grab.start_resize(view, edges)
        {
            self.resizing_id = id;
            return true;
        }
        return false;
    }

    pub fn get_resize_edges(&self) -> LabEdge {
        self.grab.get_resize_edges()
    }

    pub fn continue_resize(&mut self, cursor_x: i32, cursor_y: i32) -> UpdateLevel {
        if let Some(view) = self.by_id.get_mut(&self.resizing_id) {
            return self.grab.continue_resize(view, cursor_x, cursor_y);
        }
        return UpdateLevel::None;
    }

    pub fn snap_to_edge(&mut self, cursor_x: i32, cursor_y: i32) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        if let Some(view) = self.by_id.get_mut(&self.moving_id)
            && view.get_state().floating()
        {
            let (output, edges) = get_snap_target(cursor_x, cursor_y);
            if edges != LAB_EDGE_NONE {
                if edges == LAB_EDGE_TOP {
                    ul |= view.maximize(VIEW_AXIS_BOTH, /* is_moving */ true, output);
                } else if edges == LAB_EDGE_BOTTOM {
                    // Minimize but restore position from start of drag
                    // (or natural geometry if view was maximized/tiled)
                    ul |= view.move_resize(view.get_state().natural_geom);
                    ul |= self.minimize(self.moving_id, true).1;
                } else {
                    ul |= view.tile(edges, /* is_moving */ true, output);
                }
            }
        }
        return ul;
    }

    // Resets grab for any view if id is None
    pub fn reset_grab_for(&mut self, id: Option<ViewId>) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        if id == Some(self.grabbed_id) || id.is_none() {
            // Focus was only overridden if move/resize was actually started
            // FIXME: seat_focus_override_begin() is still called from C code
            if self.moving_id != 0 || self.resizing_id != 0 {
                unsafe {
                    seat_focus_override_end(/* restore_focus */ true)
                };
                ul |= UpdateLevel::Cursor;
            }
            self.grabbed_id = 0;
            self.moving_id = 0;
            self.resizing_id = 0;
            self.grab = ViewGrab::default();
        }
        return ul;
    }

    pub fn build_cycle_list(&mut self) {
        self.cycle_list.clear();
        for &i in self.order.iter().rev() {
            if let Some(v) = self.by_id.get(&i)
                && v.get_state().focusable()
            {
                self.cycle_list.push(i);
            }
        }
    }

    pub fn cycle_list_len(&self) -> usize {
        self.cycle_list.len()
    }

    pub fn cycle_list_nth(&self, n: usize) -> ViewId {
        *self.cycle_list.get(n).unwrap_or(&0)
    }
}
