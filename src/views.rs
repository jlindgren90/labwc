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

    // Returns CView pointer to pass to view_notify_map()
    pub fn map_common(&mut self, id: ViewId, focus_mode: ViewFocusMode) -> *mut CView {
        if let Some(view) = self.by_id.get_mut(&id) {
            let view_ptr = view.set_mapped(focus_mode);
            // Only focusable views should be shown in taskbars etc.
            if view.get_state().focusable() {
                for &client in &self.foreign_toplevel_clients {
                    view.add_foreign_toplevel(client);
                }
            }
            return view_ptr;
        }
        return null_mut();
    }

    // Returns CView pointer to pass to view_notify_unmap()
    pub fn unmap_common(&mut self, id: ViewId) -> *mut CView {
        if let Some(view) = self.by_id.get_mut(&id) {
            return view.set_unmapped();
        }
        return null_mut();
    }

    pub fn adjust_for_layout_change(&mut self) {
        for v in self.by_id.values_mut() {
            v.adjust_for_layout_change();
        }
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
