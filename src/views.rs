// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::view::*;
use std::collections::{BTreeMap, HashSet};
use std::ptr::null_mut;

#[derive(Default)]
pub struct Views {
    by_id: BTreeMap<ViewId, View>, // in creation order
    max_used_id: ViewId,
    order: Vec<ViewId>, // from back to front
    active_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
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

    pub fn get_root_of(&self, id: ViewId) -> ViewId {
        self.by_id.get(&id).map_or(0, View::get_root_id)
    }

    pub fn get_modal_dialog(&self, id: ViewId) -> Option<ViewId> {
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

    // Returns CView pointer to pass to view_notify_map()
    pub fn map_common(&mut self, id: ViewId, focus_mode: ViewFocusMode) -> *mut CView {
        if let Some(view) = self.by_id.get_mut(&id) {
            let mut was_shown = false;
            view.set_mapped(focus_mode, &mut was_shown);
            // Only focusable views should be shown in taskbars etc.
            if view.get_state().focusable() {
                for &client in &self.foreign_toplevel_clients {
                    view.add_foreign_toplevel(client);
                }
            }
            if was_shown {
                let view_ptr = view.get_c_ptr();
                self.update_top_layer_visibility();
                return view_ptr;
            }
        }
        return null_mut();
    }

    // Returns CView pointer to pass to view_notify_unmap()
    pub fn unmap_common(&mut self, id: ViewId) -> *mut CView {
        if let Some(view) = self.by_id.get_mut(&id) {
            let mut was_hidden = false;
            view.set_unmapped(&mut was_hidden);
            if was_hidden {
                let view_ptr = view.get_c_ptr();
                self.update_top_layer_visibility();
                return view_ptr;
            }
        }
        return null_mut();
    }

    pub fn get_active(&self) -> *mut CView {
        let active = self.by_id.get(&self.active_id);
        return active.map_or(null_mut(), View::get_c_ptr);
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
            self.update_top_layer_visibility();
        }
        return view_ptr;
    }

    // Returns CView pointer to pass to view_notify_minimize()
    pub fn minimize(&mut self, id: ViewId, minimized: bool) -> *mut CView {
        if let Some(view) = self.by_id.get(&id)
            && view.get_state().minimized != minimized
        {
            // Minimize/unminimize all related views together
            let view_ptr = view.get_c_ptr();
            let root = view.get_root_id();
            let mut visibility_changed = false;
            for v in self.by_id.values_mut().filter(|v| v.get_root_id() == root) {
                v.set_minimized(minimized, &mut visibility_changed);
            }
            if visibility_changed {
                self.update_top_layer_visibility();
                return view_ptr;
            }
        }
        return null_mut();
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

    pub fn add_foreign_toplevel_client(&mut self, client: *mut WlResource) {
        self.foreign_toplevel_clients.push(client);
        for view in self.by_id.values_mut() {
            if view.get_state().focusable() {
                view.add_foreign_toplevel(client);
            }
        }
    }

    pub fn remove_foreign_toplevel_client(&mut self, client: *mut WlResource) {
        self.foreign_toplevel_clients.retain(|&c| c != client);
    }
}
