// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::lazy_static;
use crate::util::*;
use crate::view::*;
use std::collections::{BTreeMap, HashSet};
use std::ffi::c_char;

#[derive(Default)]
struct Views {
    by_id: BTreeMap<ViewId, View>, // in creation order
    max_used_id: ViewId,
    order: Vec<ViewId>, // from back to front
    active_id: ViewId,
    foreign_toplevel_clients: Vec<*mut WlResource>,
    cycle_list: Vec<ViewId>, // TODO: move elsewhere?
}

impl Views {
    fn get_c_ptr(&self, id: ViewId) -> *mut CView {
        let view = self.by_id.get(&id);
        return view.map_or(std::ptr::null_mut(), View::get_c_ptr);
    }

    fn get_root_of(&self, id: ViewId) -> ViewId {
        self.by_id.get(&id).map_or(0, View::get_root_id)
    }

    fn get_modal_dialog(&self, id: ViewId) -> Option<ViewId> {
        if let Some(view) = self.by_id.get(&id) {
            // Check if view itself is a modal dialog
            if view.is_modal_dialog() {
                return Some(id);
            }
            // Check child/sibling views (in reverse stacking order)
            let root = view.get_root_id();
            for &i in self.order.iter().rev().filter(|&&i| i != id && i != root) {
                if let Some(v) = self.by_id.get(&i) {
                    if v.get_root_id() == root && v.is_modal_dialog() {
                        return Some(i);
                    }
                }
            }
        }
        return None;
    }

    fn is_active_visible(&self) -> bool {
        let active = self.by_id.get(&self.active_id);
        return active.map_or(false, |a| a.get_state().visible());
    }

    fn set_active(&mut self, id: ViewId) {
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

    // Returns true if visibility of any view changed
    fn minimize(&mut self, id: ViewId, minimized: bool) -> bool {
        if let Some(view) = self.by_id.get(&id) {
            if view.get_state().minimized != minimized {
                // Minimize/unminimize all related views together
                let root = view.get_root_id();
                let mut visibility_changed = false;
                for v in self.by_id.values_mut().filter(|v| v.get_root_id() == root) {
                    v.set_minimized(minimized, &mut visibility_changed);
                }
                if visibility_changed {
                    self.update_top_layer_visibility();
                    return true;
                }
            }
        }
        return false;
    }

    fn raise(&mut self, id: ViewId) {
        // Check if view or a sub-view is already in front
        if let Some(&front) = self.order.last() {
            if id == front || self.get_root_of(front) == id {
                return;
            }
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

    fn focus(&mut self, id: ViewId, raise: bool) {
        // Focus a modal dialog rather than its parent view
        let id_to_focus = self.get_modal_dialog(id).unwrap_or(id);
        if raise {
            self.raise(id_to_focus);
        }
        if let Some(view) = self.by_id.get_mut(&id_to_focus) {
            if view.focus() {
                self.set_active(id_to_focus);
            }
        }
    }

    fn focus_topmost(&mut self) {
        for &i in self.order.iter().rev() {
            if let Some(state) = self.by_id.get(&i).map(View::get_state) {
                if view_is_focusable(state) && !state.minimized {
                    self.focus(i, /* raise */ true);
                    return;
                }
            }
        }
        if unsafe { view_focus_impl(std::ptr::null_mut()) } {
            self.set_active(0);
        }
    }
}

lazy_static!(VIEWS, Views, Views::default(), views, views_mut);

#[no_mangle]
pub extern "C" fn view_add(c_ptr: *mut CView, is_xwayland: bool) -> ViewId {
    let mut views = views_mut();
    views.max_used_id += 1;
    let id = views.max_used_id;
    views.by_id.insert(id, View::new(c_ptr, is_xwayland));
    views.order.push(id);
    return id;
}

#[no_mangle]
pub extern "C" fn view_remove(id: ViewId) {
    let mut views = views_mut();
    views.by_id.remove(&id);
    views.order.retain(|&i| i != id);
}

#[no_mangle]
pub extern "C" fn view_count() -> usize {
    views().order.len()
}

#[no_mangle]
pub extern "C" fn view_nth(n: usize) -> *mut CView {
    let views = views();
    return views.get_c_ptr(*views.order.get(n).unwrap_or(&0));
}

#[no_mangle]
pub extern "C" fn view_get_state(id: ViewId) -> *const ViewState {
    let views = views();
    let view = views.by_id.get(&id);
    return view.map_or(std::ptr::null(), |v| v.get_state());
}

#[no_mangle]
pub extern "C" fn view_get_root(id: ViewId) -> *mut CView {
    let views = views();
    return views.get_c_ptr(views.by_id.get(&id).map_or(0, View::get_root_id));
}

#[no_mangle]
pub extern "C" fn view_get_modal_dialog(id: ViewId) -> *mut CView {
    let views = views();
    return views.get_c_ptr(views.get_modal_dialog(id).unwrap_or(0));
}

#[no_mangle]
pub extern "C" fn view_get_size_hints(id: ViewId) -> ViewSizeHints {
    let views = views();
    let view = views.by_id.get(&id);
    return view.map_or(ViewSizeHints::default(), View::get_size_hints);
}

#[no_mangle]
pub extern "C" fn view_has_strut_partial(id: ViewId) -> bool {
    let views = views();
    let view = views.by_id.get(&id);
    return view.map_or(false, View::has_strut_partial);
}

#[no_mangle]
pub extern "C" fn view_set_app_id(id: ViewId, app_id: *const c_char) {
    let mut views = views_mut();
    if let Some(view) = views.by_id.get_mut(&id) {
        let view_ptr = view.set_app_id(cstring(app_id));
        if !view_ptr.is_null() {
            drop(views); // FIXME: to allow reentrant borrow
            unsafe { view_notify_icon_change(view_ptr) };
        }
    }
}

#[no_mangle]
pub extern "C" fn view_set_title(id: ViewId, title: *const c_char) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_title(cstring(title));
    }
}

#[no_mangle]
pub extern "C" fn view_map_common(id: ViewId, focus_mode: ViewFocusMode) {
    let mut views = views_mut();
    let Views {
        by_id,
        foreign_toplevel_clients,
        ..
    } = &mut *views;
    if let Some(view) = by_id.get_mut(&id) {
        let mut was_shown = false;
        view.set_mapped(focus_mode, &mut was_shown);
        // Only focusable views should be shown in taskbars etc.
        if view_is_focusable(view.get_state()) {
            for &mut client in foreign_toplevel_clients {
                view.add_foreign_toplevel(client, id);
            }
        }
        if was_shown {
            views.update_top_layer_visibility();
            views.focus(id, /* raise */ true);
        }
    }
}

#[no_mangle]
pub extern "C" fn view_unmap_common(id: ViewId) {
    let mut views = views_mut();
    if let Some(view) = views.by_id.get_mut(&id) {
        let mut was_hidden = false;
        view.set_unmapped(&mut was_hidden);
        if was_hidden {
            views.update_top_layer_visibility();
            if !views.is_active_visible() {
                views.focus_topmost();
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn view_get_active() -> *mut CView {
    let views = views();
    let active = views.by_id.get(&views.active_id);
    return active.map_or(std::ptr::null_mut(), View::get_c_ptr);
}

#[no_mangle]
pub extern "C" fn view_set_active(id: ViewId) {
    views_mut().set_active(id);
}

#[no_mangle]
pub extern "C" fn view_set_maximized(id: ViewId, maximized: ViewAxis) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_maximized(maximized);
    }
}

#[no_mangle]
pub extern "C" fn view_set_tiled(id: ViewId, tiled: LabEdge) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_tiled(tiled);
    }
}

#[no_mangle]
pub extern "C" fn view_compute_default_geom(id: ViewId, geom: &mut Rect) {
    if let Some(view) = views().by_id.get(&id) {
        view.compute_default_geom(geom, /* rel_to */ None, /* keep_position */ false);
    }
}

#[no_mangle]
pub extern "C" fn view_set_current_pos(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_current_pos(x, y);
    }
}

#[no_mangle]
pub extern "C" fn view_set_current_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_current_size(width, height);
    }
}

#[no_mangle]
pub extern "C" fn view_set_pending_pos(id: ViewId, x: i32, y: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_pending_pos(x, y);
    }
}

#[no_mangle]
pub extern "C" fn view_set_pending_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_pending_size(width, height);
    }
}

#[no_mangle]
pub extern "C" fn view_move_resize(id: ViewId, geom: Rect) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.move_resize(geom);
    }
}

#[no_mangle]
pub extern "C" fn view_commit_size(id: ViewId, width: i32, height: i32) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.commit_size(width, height);
    }
}

#[no_mangle]
pub extern "C" fn view_set_initial_geom(id: ViewId, rel_to: Option<&Rect>, keep_position: bool) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_initial_geom(rel_to, keep_position);
    }
}

#[no_mangle]
pub extern "C" fn view_store_natural_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.store_natural_geom();
    }
}

#[no_mangle]
pub extern "C" fn view_apply_special_geom(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.apply_special_geom();
    }
}

#[no_mangle]
pub extern "C" fn view_set_output(id: ViewId, output: *mut Output) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.set_output(output);
    }
}

#[no_mangle]
pub extern "C" fn views_update_top_layer_visibility() {
    views().update_top_layer_visibility();
}

#[no_mangle]
pub extern "C" fn views_adjust_for_layout_change() {
    let mut views = views_mut();
    for v in views.by_id.values_mut() {
        v.adjust_for_layout_change();
    }
    views.update_top_layer_visibility();
}

#[no_mangle]
pub extern "C" fn view_fullscreen(id: ViewId, fullscreen: bool) {
    let mut views = views_mut();
    if let Some(view) = views.by_id.get_mut(&id) {
        if view.fullscreen(fullscreen) {
            views.update_top_layer_visibility();
            unsafe { cursor_update_focus() };
        }
    }
}

#[no_mangle]
pub extern "C" fn view_maximize(id: ViewId, axis: ViewAxis) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.maximize(axis);
    }
}

#[no_mangle]
pub extern "C" fn view_minimize(id: ViewId, minimized: bool) {
    let mut views = views_mut();
    if views.minimize(id, minimized) {
        if !minimized {
            views.focus(id, /* raise */ true);
        } else if !views.is_active_visible() {
            views.focus_topmost();
        }
    }
}

#[no_mangle]
pub extern "C" fn view_raise(id: ViewId) {
    views_mut().raise(id);
}

#[no_mangle]
pub extern "C" fn view_focus(id: ViewId, raise: bool) {
    let mut views = views_mut();
    views.minimize(id, false);
    views.focus(id, raise);
}

#[no_mangle]
pub extern "C" fn view_focus_topmost() {
    views_mut().focus_topmost();
}

#[no_mangle]
pub extern "C" fn view_close(id: ViewId) {
    if let Some(view) = views().by_id.get(&id) {
        view.close();
    }
}

#[no_mangle]
pub extern "C" fn views_add_foreign_toplevel_client(client: *mut WlResource) {
    let Views {
        by_id,
        foreign_toplevel_clients,
        ..
    } = &mut *views_mut();
    foreign_toplevel_clients.push(client);
    for (&id, view) in by_id {
        if view_is_focusable(view.get_state()) {
            view.add_foreign_toplevel(client, id);
        }
    }
}

#[no_mangle]
pub extern "C" fn views_remove_foreign_toplevel_client(client: *mut WlResource) {
    let mut views = views_mut();
    views.foreign_toplevel_clients.retain(|&c| c != client);
}

#[no_mangle]
pub extern "C" fn view_remove_foreign_toplevel(id: ViewId, resource: *mut WlResource) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.remove_foreign_toplevel(resource);
    }
}

// Transfers ownership of the surface to the view
#[no_mangle]
pub extern "C" fn view_add_icon_surface(id: ViewId, surface: *mut CairoSurface) {
    let surf = CairoSurfacePtr::new(surface);
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.add_icon_surface(surf);
    }
}

#[no_mangle]
pub extern "C" fn view_clear_icon_surfaces(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.clear_icon_surfaces();
    }
}

// Does NOT transfer ownership out of the view
#[no_mangle]
pub extern "C" fn view_get_icon_buffer(id: ViewId, icon_size: i32, scale: f32) -> *mut WlrBuffer {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.get_icon_buffer(icon_size, scale)
    } else {
        std::ptr::null_mut()
    }
}

#[no_mangle]
pub extern "C" fn view_drop_icon_buffer(id: ViewId) {
    if let Some(view) = views_mut().by_id.get_mut(&id) {
        view.drop_icon_buffer();
    }
}

#[no_mangle]
pub extern "C" fn cycle_list_build() {
    let Views {
        by_id,
        order,
        cycle_list,
        ..
    } = &mut *views_mut();
    cycle_list.clear();
    for &i in order.iter().rev() {
        if let Some(v) = by_id.get(&i) {
            if view_is_focusable(v.get_state()) {
                cycle_list.push(i);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn cycle_list_len() -> usize {
    views().cycle_list.len()
}

#[no_mangle]
pub extern "C" fn cycle_list_nth(n: usize) -> *mut CView {
    let views = views();
    return views.get_c_ptr(*views.cycle_list.get(n).unwrap_or(&0));
}
