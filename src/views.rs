// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::view::*;
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
        self.by_id.insert(id, View::new(c_ptr, is_xwayland));
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

    pub fn get_c_ptr(&self, id: ViewId) -> *mut CView {
        self.by_id.get(&id).map_or(null_mut(), View::get_c_ptr)
    }

    pub fn count(&self) -> usize {
        self.order.len()
    }

    pub fn get_nth(&self, n: usize) -> *mut CView {
        self.get_c_ptr(*self.order.get(n).unwrap_or(&0))
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

    pub fn map_common(&mut self, id: ViewId, focus_mode: ViewFocusMode) {
        if let Some(view) = self.by_id.get_mut(&id) {
            let mut was_shown = false;
            view.set_mapped(focus_mode, &mut was_shown);
            // Only focusable views should be shown in taskbars etc.
            if view.get_state().focusable() {
                for &client in &self.foreign_toplevel_clients {
                    view.add_foreign_toplevel(client, id);
                }
            }
            if was_shown {
                self.update_top_layer_visibility();
                self.focus(id, /* raise */ true);
            }
        }
    }

    pub fn unmap_common(&mut self, id: ViewId) {
        if let Some(view) = self.by_id.get_mut(&id) {
            let mut was_hidden = false;
            view.set_unmapped(&mut was_hidden);
            if was_hidden {
                self.update_top_layer_visibility();
                self.reset_grab_for(Some(id));
                if !self.is_active_visible() {
                    self.focus_topmost();
                }
            }
        }
    }

    pub fn get_active(&self) -> *mut CView {
        let active = self.by_id.get(&self.active_id);
        return active.map_or(null_mut(), View::get_c_ptr);
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

    pub fn commit_geom(&mut self, id: ViewId, width: i32, height: i32) {
        let mut resize_edges = LAB_EDGE_NONE;
        if id == self.resizing_id {
            resize_edges = self.grab.get_resize_edges();
        }
        if let Some(view) = self.by_id.get_mut(&id) {
            view.commit_geom(width, height, resize_edges);
        }
    }

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

    pub fn adjust_for_layout_change(&mut self) {
        for v in self.by_id.values_mut() {
            v.adjust_for_layout_change();
        }
        self.update_top_layer_visibility();
    }

    // Returns CView pointer to pass to view_notify_fullscreen()
    pub fn fullscreen(&mut self, id: ViewId, fullscreen: bool) -> *mut CView {
        let Some(view) = self.by_id.get_mut(&id) else {
            return null_mut();
        };
        let view_ptr = view.fullscreen(fullscreen);
        if !view_ptr.is_null() {
            // Entering/leaving fullscreen ends any interactive move/resize
            self.reset_grab_for(Some(id));
            self.update_top_layer_visibility();
        }
        return view_ptr;
    }

    pub fn maximize(&mut self, id: ViewId, axis: ViewAxis) {
        if let Some(view) = self.by_id.get_mut(&id)
            && view.get_state().maximized != axis
        {
            view.maximize(axis, id == self.moving_id);
            // Maximizing/unmaximizing ends any interactive move/resize
            self.reset_grab_for(Some(id));
        }
    }

    pub fn tile(&mut self, id: ViewId, edge: LabEdge) {
        if let Some(view) = self.by_id.get_mut(&id)
            && view.get_state().tiled != edge
        {
            view.tile(edge, id == self.moving_id);
            // Tiling/untiling ends any interactive move/resize
            self.reset_grab_for(Some(id));
        }
    }

    // Returns true if visibility of any view changed
    pub fn minimize(&mut self, id: ViewId, minimized: bool) -> bool {
        let mut visibility_changed = false;
        if let Some(view) = self.by_id.get(&id)
            && view.get_state().minimized != minimized
        {
            // Minimize/unminimize all related views together
            let root = view.get_root_id();
            let mut reset_grab = false;
            for (&i, v) in &mut self.by_id {
                if v.get_root_id() == root {
                    v.set_minimized(minimized, &mut visibility_changed);
                    reset_grab |= i == self.grabbed_id;
                }
            }
            // Minimize ends any interactive move/resize
            if reset_grab {
                self.reset_grab_for(None);
            }
        }
        if visibility_changed {
            if !minimized {
                self.focus(id, /* raise */ true);
            } else if !self.is_active_visible() {
                self.focus_topmost();
            }
            // Might be redundant after raise, but be sure
            self.update_top_layer_visibility();
        }
        return visibility_changed;
    }

    pub fn raise(&mut self, id: ViewId) {
        // Check if view or a sub-view is already in front
        if let Some(&front) = self.order.last()
            && (id == front || self.get_root_of(front) == id)
        {
            return;
        }
        let Some(view) = self.by_id.get(&id) else {
            return;
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
        self.update_top_layer_visibility();
        unsafe { cursor_update_focus() };
    }

    pub fn focus(&mut self, id: ViewId, raise: bool) {
        // Focus a modal dialog rather than its parent view
        let id_to_focus = self.get_modal_dialog(id).unwrap_or(id);
        if raise {
            self.raise(id_to_focus);
        }
        if let Some(view) = self.by_id.get_mut(&id_to_focus)
            && view.focus()
        {
            self.set_active(id_to_focus);
        }
    }

    pub fn focus_topmost(&mut self) {
        for &i in self.order.iter().rev() {
            if let Some(state) = self.by_id.get(&i).map(View::get_state)
                && state.focusable()
                && !state.minimized
            {
                self.focus(i, /* raise */ true);
                return;
            }
        }
        if unsafe { view_focus_impl(null_mut()) } {
            self.set_active(0);
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

    pub fn get_moving(&self) -> *mut CView {
        let moving = self.by_id.get(&self.moving_id);
        return moving.map_or(null_mut(), View::get_c_ptr);
    }

    pub fn adjust_move_origin(&mut self, width: i32, height: i32) {
        self.grab.adjust_move_origin(width, height);
    }

    pub fn compute_move_position(&self, cursor_x: i32, cursor_y: i32) -> (i32, i32) {
        self.grab.compute_move_position(cursor_x, cursor_y)
    }

    pub fn continue_move(&mut self, cursor_x: i32, cursor_y: i32) {
        if let Some(view) = self.by_id.get_mut(&self.moving_id) {
            self.grab.continue_move(view, cursor_x, cursor_y);
        }
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

    pub fn get_resizing(&self) -> *mut CView {
        let resizing = self.by_id.get(&self.resizing_id);
        return resizing.map_or(null_mut(), View::get_c_ptr);
    }

    pub fn get_resize_edges(&self) -> LabEdge {
        self.grab.get_resize_edges()
    }

    pub fn continue_resize(&mut self, cursor_x: i32, cursor_y: i32) {
        if let Some(view) = self.by_id.get_mut(&self.resizing_id) {
            self.grab.continue_resize(view, cursor_x, cursor_y);
        }
    }

    pub fn snap_to_edge(&mut self, cursor_x: i32, cursor_y: i32) {
        if let Some(view) = self.by_id.get_mut(&self.moving_id)
            && view.get_state().floating()
        {
            let (output, edges) = get_snap_target(cursor_x, cursor_y);
            if edges != LAB_EDGE_NONE {
                view.set_output(output);
                if edges == LAB_EDGE_TOP {
                    view.maximize(VIEW_AXIS_BOTH, /* is_moving */ true);
                } else if edges == LAB_EDGE_BOTTOM {
                    // Minimize but restore position from start of drag
                    // (or natural geometry if view was maximized/tiled)
                    view.move_resize(view.get_state().natural_geom);
                    self.minimize(self.moving_id, true);
                } else {
                    view.tile(edges, /* is_moving */ true);
                }
            }
        }
    }

    // Resets grab for any view if id is None
    pub fn reset_grab_for(&mut self, id: Option<ViewId>) {
        if id == Some(self.grabbed_id) || id.is_none() {
            // Focus was only overridden if move/resize was actually started
            // FIXME: seat_focus_override_begin() is still called from C code
            if self.moving_id != 0 || self.resizing_id != 0 {
                unsafe { seat_focus_override_end() };
            }
            self.grabbed_id = 0;
            self.moving_id = 0;
            self.resizing_id = 0;
            self.grab = ViewGrab::default();
        }
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

    pub fn cycle_list_nth(&self, n: usize) -> *mut CView {
        self.get_c_ptr(*self.cycle_list.get(n).unwrap_or(&0))
    }
}
