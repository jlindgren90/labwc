// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::rect::*;
use crate::xwm::*;
use std::ptr::null_mut;

pub trait ViewImpl {
    fn get_surface(&self) -> *mut WlrSurface;
    fn get_xid(&self) -> XId;
    fn get_parent(&self) -> ViewId;
    fn get_root_id(&self, xwm: &Xwm) -> ViewId;
    fn get_modal(&self) -> Option<bool>;
    fn get_size_hints(&self) -> ViewSizeHints;
    fn set_size_hints(&mut self, hints: &ViewSizeHints);
    fn set_bool_prop(&mut self, prop: XBoolProp, val: bool);
    fn notify_mapped(&self, state: &ViewState);
    fn set_active(&self, state: &ViewState);
    fn set_fullscreen(&mut self, state: &ViewState);
    fn set_maximized(&self, state: &ViewState);
    fn notify_tiled(&self);
    fn set_minimized(&self, state: &ViewState, xwm: &mut Xwm);
    fn configure(&self, current: Rect, geom: Rect, commit_move: *mut bool);
    fn raise(&self, xwm: &mut Xwm);
    fn wants_focus(&self) -> bool;
    fn focus(&self, xwm: &mut Xwm) -> bool;
    fn close(&self);

    // scene-tree helpers
    fn create_scene(&self, scene: &mut ViewScene, id: ViewId);
    fn map_scene_surface(&self, scene: &mut ViewScene);
    fn unmap_scene_surface(&self, scene: &mut ViewScene);
    fn adjust_scene_pos(
        &mut self,
        state: &ViewState,
        scene: &ViewScene,
        x: i32,
        y: i32,
    ) -> (i32, i32);
}

#[derive(Default)]
pub struct XView {
    xid: XId,
    xsurface: *mut XSurface,
    size_hints: ViewSizeHints,
    input_hint: bool,
    normal_or_dialog: bool,
    supports_delete: bool,
    supports_take_focus: bool,
}

impl XView {
    pub fn new(xid: XId, xsurface: *mut XSurface) -> Self {
        Self {
            xid: xid,
            xsurface: xsurface,
            input_hint: true, // assumed set by default
            ..Self::default()
        }
    }

    fn publish_state_if_mapped(&self, state: &ViewState) {
        if state.mapped {
            unsafe { xwayland_publish_window_state(self.xid, state) };
        }
    }
}

impl ViewImpl for XView {
    fn get_surface(&self) -> *mut WlrSurface {
        unsafe { xwayland_surface_get_surface(self.xsurface) }
    }

    fn get_xid(&self) -> XId {
        self.xid
    }

    fn get_parent(&self) -> ViewId {
        0 // not currently needed
    }

    fn get_root_id(&self, xwm: &Xwm) -> ViewId {
        xwm.get_root_view_id(self.xid)
    }

    fn get_modal(&self) -> Option<bool> {
        None // not supported (modal state is set explicitly)
    }

    fn get_size_hints(&self) -> ViewSizeHints {
        self.size_hints
    }

    fn set_size_hints(&mut self, hints: &ViewSizeHints) {
        self.size_hints = *hints;
    }

    fn set_bool_prop(&mut self, prop: XBoolProp, val: bool) {
        match prop {
            XBOOL_PROP_INPUT_HINT => self.input_hint = val,
            XBOOL_PROP_NORMAL_OR_DIALOG => self.normal_or_dialog = val,
            XBOOL_PROP_SUPPORTS_DELETE => self.supports_delete = val,
            XBOOL_PROP_SUPPORTS_TAKE_FOCUS => self.supports_take_focus = val,
            _ => return, // invalid
        };
    }

    fn notify_mapped(&self, state: &ViewState) {
        self.publish_state_if_mapped(state);
    }

    fn set_active(&self, state: &ViewState) {
        if state.active {
            unsafe { xwayland_set_active_window(self.xid) };
        }
        self.publish_state_if_mapped(state);
    }

    fn set_fullscreen(&mut self, state: &ViewState) {
        self.publish_state_if_mapped(state);
    }

    fn set_maximized(&self, state: &ViewState) {
        self.publish_state_if_mapped(state);
    }

    fn notify_tiled(&self) {
        // not supported
    }

    fn set_minimized(&self, state: &ViewState, xwm: &mut Xwm) {
        if state.minimized {
            // restack minimized view to bottom
            xwm.lower(self.xid);
        }
        self.publish_state_if_mapped(state);
    }

    fn configure(&self, current: Rect, geom: Rect, commit_move: *mut bool) {
        unsafe { xwayland_view_configure(self.xid, current, geom, commit_move) };
    }

    fn raise(&self, xwm: &mut Xwm) {
        xwm.raise(self.xid);
    }

    fn wants_focus(&self) -> bool {
        if self.input_hint {
            // Input hint set in WM_HINTS -> WM sets input focus.
            // Assuming this case also if WM_HINTS is missing altogether.
            return true;
        }
        if self.supports_take_focus {
            // WM_TAKE_FOCUS set in WM_PROTOCOLS -> client sets focus.
            // Guessing whether it wants focus based on window type.
            return self.normal_or_dialog;
        }
        // client doesn't want input focus at all
        return false;
    }

    fn focus(&self, xwm: &mut Xwm) -> bool {
        if xwm.focus(self.xid, self.input_hint, self.supports_take_focus) {
            unsafe { seat_focus_surface_no_notify(self.get_surface()) };
            return true;
        }
        return false;
    }

    fn close(&self) {
        if self.supports_delete {
            unsafe { xwayland_delete_window(self.xid) };
        } else {
            unsafe { xwayland_kill_window(self.xid) };
        }
    }

    fn create_scene(&self, scene: &mut ViewScene, id: ViewId) {
        scene.scene_tree = unsafe { view_scene_tree_create(id) };
    }

    fn map_scene_surface(&self, scene: &mut ViewScene) {
        scene.surface_tree =
            unsafe { view_surface_tree_create(scene.scene_tree, self.get_surface()) };
    }

    fn unmap_scene_surface(&self, scene: &mut ViewScene) {
        unsafe { view_scene_tree_destroy(scene.surface_tree) };
        scene.surface_tree = null_mut();
    }

    fn adjust_scene_pos(
        &mut self,
        _state: &ViewState,
        _scene: &ViewScene,
        x: i32,
        y: i32,
    ) -> (i32, i32) {
        (x, y)
    }
}

pub struct XdgView {
    c_ptr: *mut CView,
    fullscreen_bg: *mut WlrSceneRect,
}

impl XdgView {
    pub fn new(c_ptr: *mut CView) -> Self {
        Self {
            c_ptr: c_ptr,
            fullscreen_bg: null_mut(),
        }
    }

    fn hide_fullscreen_bg(&mut self) {
        if !self.fullscreen_bg.is_null() {
            unsafe { view_fullscreen_bg_hide(self.fullscreen_bg) };
        }
    }
}

impl ViewImpl for XdgView {
    fn get_surface(&self) -> *mut WlrSurface {
        unsafe { xdg_toplevel_view_get_surface(self.c_ptr) }
    }

    fn get_xid(&self) -> XId {
        0 // None
    }

    fn get_parent(&self) -> ViewId {
        unsafe { xdg_toplevel_view_get_parent(self.c_ptr) }
    }

    fn get_root_id(&self, _xwm: &Xwm) -> ViewId {
        unsafe { xdg_toplevel_view_get_root_id(self.c_ptr) }
    }

    fn get_modal(&self) -> Option<bool> {
        Some(unsafe { xdg_toplevel_view_is_modal_dialog(self.c_ptr) })
    }

    fn get_size_hints(&self) -> ViewSizeHints {
        unsafe { xdg_toplevel_view_get_size_hints(self.c_ptr) }
    }

    fn set_size_hints(&mut self, _hints: &ViewSizeHints) {
        // not supported
    }

    fn set_bool_prop(&mut self, _prop: XBoolProp, _val: bool) {
        // not supported
    }

    fn notify_mapped(&self, _state: &ViewState) {
        // no-op
    }

    fn set_active(&self, state: &ViewState) {
        unsafe { xdg_toplevel_view_set_active(self.c_ptr, state.active) };
    }

    fn set_fullscreen(&mut self, state: &ViewState) {
        unsafe { xdg_toplevel_view_set_fullscreen(self.c_ptr, state.fullscreen) };
        if !state.fullscreen {
            self.hide_fullscreen_bg();
        }
    }

    fn set_maximized(&self, state: &ViewState) {
        unsafe { xdg_toplevel_view_maximize(self.c_ptr, state.maximized) };
    }

    fn notify_tiled(&self) {
        unsafe { xdg_toplevel_view_notify_tiled(self.c_ptr) };
    }

    fn set_minimized(&self, _state: &ViewState, _xwm: &mut Xwm) {
        // not supported
    }

    fn configure(&self, _current: Rect, geom: Rect, commit_move: *mut bool) {
        unsafe {
            xdg_toplevel_view_configure(self.c_ptr, geom, commit_move);
        }
    }

    fn raise(&self, _xwm: &mut Xwm) {
        // not supported
    }

    fn wants_focus(&self) -> bool {
        true
    }

    fn focus(&self, _xwm: &mut Xwm) -> bool {
        unsafe { seat_focus_surface_no_notify(self.get_surface()) };
        return true;
    }

    fn close(&self) {
        unsafe { xdg_toplevel_view_close(self.c_ptr) };
    }

    fn create_scene(&self, scene: &mut ViewScene, id: ViewId) {
        scene.scene_tree = unsafe { view_scene_tree_create(id) };
        scene.surface_tree =
            unsafe { view_surface_tree_create(scene.scene_tree, self.get_surface()) };
    }

    fn map_scene_surface(&self, _scene: &mut ViewScene) {
        // no-op
    }

    fn unmap_scene_surface(&self, _scene: &mut ViewScene) {
        // no-op
    }

    fn adjust_scene_pos(
        &mut self,
        state: &ViewState,
        scene: &ViewScene,
        x: i32,
        y: i32,
    ) -> (i32, i32) {
        if !state.fullscreen {
            return (x, y);
        }
        let output_geom = unsafe { output_layout_coords(state.output) };
        if rect_empty(output_geom) {
            self.hide_fullscreen_bg();
            return (x, y);
        }
        // Center fullscreen views smaller than output and add black background
        let mut geom = rect_center(state.current.width, state.current.height, output_geom);
        rect_move_within(&mut geom, output_geom);
        if geom.width < output_geom.width || geom.height < output_geom.height {
            if self.fullscreen_bg.is_null() {
                self.fullscreen_bg = unsafe { view_fullscreen_bg_create(scene.scene_tree) };
            }
            let rel_geom = Rect {
                x: output_geom.x - geom.x,
                y: output_geom.y - geom.y,
                ..output_geom
            };
            unsafe { view_fullscreen_bg_show_at(self.fullscreen_bg, rel_geom) };
        } else {
            self.hide_fullscreen_bg();
        }
        return (geom.x, geom.y);
    }
}
