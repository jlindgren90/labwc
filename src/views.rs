// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::view::*;
use crate::view_geom::*;
use crate::view_grab::*;
use crate::xwm::*;
use std::collections::{BTreeMap, HashSet};
use std::ffi::{CString, c_char};
use std::ptr::null_mut;
use std::slice;

#[derive(Default)]
pub struct Views {
    by_id: BTreeMap<ViewId, View>, // in creation order
    max_used_id: ViewId,
    // loosely back-to-front order; actually least- to most-recently
    // raised, with minimized and always-on-top views interspersed
    order: Vec<ViewId>,
    active_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
    grabbed_id: ViewId, // set at mouse button press
    moving_id: ViewId,  // set once cursor actually moves
    resizing_id: ViewId,
    grab: ViewGrab,
    cycle_list: Vec<ViewId>, // TODO: move elsewhere?
    xwm: Xwm,
}

impl Views {
    pub fn add(&mut self, spec: ViewSpec) -> ViewId {
        self.max_used_id += 1;
        let id = self.max_used_id;
        self.by_id.insert(id, View::new(id, spec));
        self.order.push(id);
        if let ViewSpec::Xwayland(xid, _) = spec {
            self.xwm.set_view_id(xid, id);
        }
        return id;
    }

    pub fn remove(&mut self, id: ViewId) {
        if let Some(view) = self.by_id.get(&id) {
            self.xwm.set_view_id(view.get_xid(), 0);
        }
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
        self.by_id.get(&id).map_or(0, |v| v.get_root_id(&self.xwm))
    }

    fn get_modal_dialog(&self, id: ViewId) -> Option<ViewId> {
        if let Some(view) = self.by_id.get(&id) {
            // Check if view itself is a modal dialog
            if view.get_state().mapped && view.get_state().modal {
                return Some(id);
            }
            // Check child/sibling views (in reverse stacking order)
            let root = view.get_root_id(&self.xwm);
            for &i in self.order.iter().rev().filter(|&&i| i != id && i != root) {
                if let Some(v) = self.by_id.get(&i)
                    && v.get_state().mapped
                    && v.get_state().modal
                    && v.get_root_id(&self.xwm) == root
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
        self.update_net_client_list();
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
        self.update_net_client_list();
        return ul;
    }

    fn update_net_client_list(&self) {
        let mut xids = Vec::new();
        for v in self.by_id.values() {
            let xid = v.get_xid();
            if xid != 0 && v.get_state().mapped {
                xids.push(xid);
            }
        }
        unsafe { xwayland_set_net_client_list(xids.as_ptr(), xids.len() as u32) };
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
            let mut active_xid = 0;
            self.active_id = id;
            if let Some(prev) = self.by_id.get_mut(&prev_id) {
                prev.set_active(false);
            }
            if let Some(view) = self.by_id.get_mut(&id) {
                view.set_active(true);
                active_xid = view.get_xid();
            }
            if active_xid == 0 {
                // clear xwayland focus if xdg-shell view (or none) active
                self.xwm.clear_focus();
            }
        }
    }

    pub fn commit_geom(
        &mut self,
        id: ViewId,
        width: i32,
        height: i32,
        only_if_changed: bool,
    ) -> UpdateLevel {
        let mut resize_edges = LAB_EDGE_NONE;
        if id == self.resizing_id {
            resize_edges = self.grab.get_resize_edges();
        }
        if let Some(view) = self.by_id.get_mut(&id) {
            let current = view.get_state().current;
            if only_if_changed && width == current.width && height == current.height {
                return UpdateLevel::None;
            }
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
            let root = view.get_root_id(&self.xwm);
            let mut reset_grab = false;
            for (&i, v) in &mut self.by_id {
                if v.get_root_id(&self.xwm) == root {
                    ul |= v.set_minimized(minimized, &mut visibility_changed, &mut self.xwm);
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
        let root = view.get_root_id(&self.xwm);
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
            v.raise(&mut self.xwm);
        }
        self.order.retain(|i| !ids_to_raise.contains(i));
        self.order.append(&mut ids_to_raise);
        // Keep always-on-top views on top by raising again
        for v in self.order.iter().filter_map(|i| self.by_id.get(i)) {
            if v.get_state().always_on_top {
                v.raise(&mut self.xwm);
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

    // No-op for unmapped views (which are raised and focused at map).
    // For minimized views, use unminimize_and_focus() instead.
    fn focus(&mut self, id: ViewId, raise: bool, force_restack: bool) -> UpdateLevel {
        // Focus a modal dialog rather than its parent view
        let id_to_focus = self.get_modal_dialog(id).unwrap_or(id);
        let ul = if raise {
            self.raise(id_to_focus, force_restack)
        } else {
            UpdateLevel::None
        };
        if let Some(view) = self.by_id.get_mut(&id_to_focus)
            && view.focus(&mut self.xwm)
        {
            self.set_active(id_to_focus);
        }
        return ul;
    }

    pub fn unminimize_and_focus(&mut self, id: ViewId, raise: bool) -> UpdateLevel {
        // Unminimizing also focuses (and raises) the view
        let (was_shown, mut ul) = self.minimize(id, false);
        if !was_shown {
            ul |= self.focus(id, raise, /* force_restack */ false);
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
        unsafe { seat_focus_surface_no_notify(null_mut()) };
        self.set_active(0);
        return UpdateLevel::None;
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

    pub fn get_xwm(&self) -> &Xwm {
        &self.xwm
    }

    pub fn add_xsurface(&mut self, xid: XId, xsurface: *mut XSurface) {
        self.xwm.add(xid, xsurface);
    }

    pub fn set_xsurface_managed(&mut self, xid: XId, managed: bool) {
        if managed {
            self.add(ViewSpec::Xwayland(xid, self.xwm.get_xsurface(xid)));
        } else if let Some(id) = self.xwm.get_view_id(xid) {
            self.remove(id)
        }
    }

    pub fn remove_xsurface(&mut self, xid: XId) {
        // remove view first
        if let Some(id) = self.xwm.get_view_id(xid) {
            self.remove(id)
        }
        self.xwm.remove(xid);
    }

    pub fn set_parent_xid(&mut self, xid: XId, parent_xid: XId) {
        self.xwm.set_parent_xid(xid, parent_xid);
    }

    pub fn set_xsurface_serial(&mut self, xid: XId, serial: u64) {
        self.xwm.set_serial(xid, serial);
    }

    pub fn set_xsurface_surface_id(&mut self, xid: XId, surface_id: u32) {
        self.xwm.set_surface_id(xid, surface_id);
    }

    pub fn set_xsurface_initial_state(&mut self, xid: XId, state: XSurfaceInitialState) {
        if let Some(id) = self.xwm.get_view_id(xid)
            && let Some(view) = self.by_id.get_mut(&id)
            && !view.get_state().mapped
        {
            view.set_modal(state.modal);
            // all UpdateLevels should be None before map, so ignoring
            _ = self.fullscreen(id, state.fullscreen, /* output */ null_mut());
            _ = self.maximize(id, state.maximized);
            _ = self.minimize(id, state.minimized);
            _ = self.set_always_on_top(id, state.always_on_top);
        }
    }

    pub fn set_xsurface_string_prop(
        &mut self,
        xid: XId,
        prop: XStringProp,
        str: *const c_char,
        len: usize,
    ) {
        if let Some(id) = self.xwm.get_view_id(xid)
            && let Some(view) = self.by_id.get_mut(&id)
        {
            // SAFETY: requires valid C string (no internal NUL)
            let slice = unsafe { slice::from_raw_parts(str as *const u8, len) };
            let string = CString::new(slice).unwrap();
            match prop {
                XSTRING_PROP_INSTANCE => view.set_app_id(string),
                XSTRING_PROP_WM_NAME => view.set_title(string, /* preferred */ false),
                XSTRING_PROP_NET_NAME => view.set_title(string, /* preferred */ true),
                _ => return, // invalid
            }
        }
    }

    pub fn map_xsurface(&mut self, xid: XId, surface: *mut WlrSurface) -> UpdateLevel {
        if let Some(id) = self.xwm.get_view_id(xid) {
            return self.map(id);
        }
        self.xwm.map_unmanaged(xid, surface);
        return UpdateLevel::Cursor;
    }

    pub fn unmap_xsurface(&mut self, xid: XId, surface: *mut WlrSurface) -> UpdateLevel {
        if let Some(id) = self.xwm.get_view_id(xid) {
            return self.unmap(id);
        }
        self.xwm.unmap_unmanaged(xid);
        // Set seat focus back to the active view (server-side focus
        // is expected to be returned automatically)
        if surface == unsafe { seat_get_focused_surface() }
            && let Some(active) = self.by_id.get(&self.active_id)
        {
            active.refocus();
        }
        return UpdateLevel::Cursor;
    }

    pub fn change_xsurface_state(
        &mut self,
        xid: XId,
        action: XStateAction,
        flags: XStateFlag,
    ) -> UpdateLevel {
        let mut ul = UpdateLevel::None;
        if let Some(id) = self.xwm.get_view_id(xid) {
            if let Some(view) = self.by_id.get_mut(&id) {
                let mut state_flags = view.get_state().to_xstate_flags();
                // Apply requested bitwise operator
                match action {
                    XSTATE_ACTION_REMOVE => state_flags &= !flags,
                    XSTATE_ACTION_ADD => state_flags |= flags,
                    XSTATE_ACTION_TOGGLE => state_flags ^= flags,
                    _ => return ul, // invalid
                }
                let modal = (state_flags & XSTATE_FLAG_MODAL) != 0;
                let fullscreen = (state_flags & XSTATE_FLAG_FULLSCREEN) != 0;
                let maximized = xstate_flags_maximized_axis(state_flags);
                let minimized = (state_flags & XSTATE_FLAG_MINIMIZED) != 0;
                let always_on_top = (state_flags & XSTATE_FLAG_ALWAYS_ON_TOP) != 0;
                // Apply updated flags back to view
                view.set_modal(modal);
                ul |= self.fullscreen(id, fullscreen, /* output */ null_mut());
                ul |= self.maximize(id, maximized);
                ul |= self.minimize(id, minimized).1;
                ul |= self.set_always_on_top(id, always_on_top);
            }
            // Normally we wait until map to update _NET_WM_STATE, but should
            // also update it for _NET_WM_STATE client message before map.
            if let Some(view) = self.by_id.get_mut(&id)
                && !view.get_state().mapped
            {
                unsafe { xwayland_publish_window_state(xid, view.get_state()) };
            }
        }
        return ul;
    }

    pub fn request_xsurface_configure(&mut self, xid: XId, geom: Rect) -> UpdateLevel {
        let xsurface = self.xwm.get_xsurface(xid);
        if let Some(id) = self.xwm.get_view_id(xid)
            && let Some(view) = self.by_id.get_mut(&id)
        {
            let state = view.get_state();
            // Allow managed surface (view) configure only if floating
            if state.floating() {
                let hints = view.get_size_hints();
                let mut adjusted = geom;
                adjust_size_for_hints(&hints, &mut adjusted.width, &mut adjusted.height);
                return view.move_resize(adjusted);
            }
            // If not floating, send back synthetic configure with existing geometry
            unsafe { xwayland_surface_configure(xsurface, state.pending) };
        } else {
            // Always allow unmanaged surface configure
            unsafe { xwayland_surface_configure(xsurface, geom) };
            return self.move_unmanaged(xid, geom.x, geom.y);
        }
        return UpdateLevel::None;
    }

    pub fn move_unmanaged(&self, xid: XId, x: i32, y: i32) -> UpdateLevel {
        self.xwm.move_unmanaged(xid, x, y);
        return UpdateLevel::Cursor;
    }

    pub fn focus_xsurface(&mut self, xid: XId, reason: XFocusReason) -> UpdateLevel {
        if let Some(id) = self.xwm.get_view_id(xid) {
            if reason == XFOCUS_REASON_ACTIVATE {
                return self.unminimize_and_focus(id, /* raise */ true);
            } else if reason == XFOCUS_REASON_FOCUS_IN {
                // Surface has already been focused server-side
                // (not unminimizing or raising in this case)
                self.xwm.set_focused(xid);
                return self.focus(id, /* raise */ false, /* force_restack */ false);
            }
        } else {
            // Let unmanaged surface take focus for any reason
            self.xwm.focus_unmanaged(xid);
        }
        return UpdateLevel::None;
    }
}
