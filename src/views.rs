// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::view::*;
use std::collections::BTreeMap;
use std::ptr::null_mut;

#[derive(Default)]
pub struct Views {
    by_id: BTreeMap<ViewId, View>, // in creation order
    max_used_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
}

impl Views {
    pub fn add(&mut self, c_ptr: *mut CView, is_xwayland: bool) -> ViewId {
        self.max_used_id += 1;
        let id = self.max_used_id;
        self.by_id.insert(id, View::new(c_ptr, is_xwayland));
        return id;
    }

    pub fn remove(&mut self, id: ViewId) {
        self.by_id.remove(&id);
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

    pub fn get_root_of(&self, id: ViewId) -> ViewId {
        self.by_id.get(&id).map_or(0, View::get_root_id)
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
                return view.get_c_ptr();
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
                return view.get_c_ptr();
            }
        }
        return null_mut();
    }

    pub fn adjust_for_layout_change(&mut self) {
        for v in self.by_id.values_mut() {
            v.adjust_for_layout_change();
        }
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
                return view_ptr;
            }
        }
        return null_mut();
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
