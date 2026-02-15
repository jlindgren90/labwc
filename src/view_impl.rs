// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::rect::*;

pub trait ViewImpl {
    fn get_surface(&self) -> *mut WlrSurface;
    fn get_root_id(&self) -> ViewId;
    fn is_modal_dialog(&self) -> bool;
    fn get_size_hints(&self) -> ViewSizeHints;
    fn has_strut_partial(&self) -> bool;
    fn set_active(&self, active: bool);
    fn set_fullscreen(&self, fullscreen: bool);
    fn set_maximized(&self, maximized: ViewAxis);
    fn notify_tiled(&self);
    fn set_minimized(&self, minimized: bool);
    fn configure(&self, geom: Rect, commit_move: *mut bool);
    fn adjust_usable_area(&self, output: *mut Output);
    fn get_focus_mode(&self) -> ViewFocusMode;
    fn offer_focus(&self);
    fn close(&self);

    // scene-tree helpers
    fn adjust_scene_pos(&mut self, state: &ViewState, x: i32, y: i32) -> (i32, i32);
}

pub struct XView {
    c_ptr: *mut CView,
}

impl XView {
    pub fn new(c_ptr: *mut CView) -> Self {
        Self { c_ptr }
    }
}

impl ViewImpl for XView {
    fn get_surface(&self) -> *mut WlrSurface {
        unsafe { xwayland_view_get_surface(self.c_ptr) }
    }

    fn get_root_id(&self) -> ViewId {
        unsafe { xwayland_view_get_root_id(self.c_ptr) }
    }

    fn is_modal_dialog(&self) -> bool {
        unsafe { xwayland_view_is_modal_dialog(self.c_ptr) }
    }

    fn get_size_hints(&self) -> ViewSizeHints {
        unsafe { xwayland_view_get_size_hints(self.c_ptr) }
    }

    fn has_strut_partial(&self) -> bool {
        unsafe { xwayland_view_has_strut_partial(self.c_ptr) }
    }

    fn set_active(&self, active: bool) {
        unsafe { xwayland_view_set_active(self.c_ptr, active) };
    }

    fn set_fullscreen(&self, fullscreen: bool) {
        unsafe { xwayland_view_set_fullscreen(self.c_ptr, fullscreen) };
    }

    fn set_maximized(&self, maximized: ViewAxis) {
        unsafe { xwayland_view_maximize(self.c_ptr, maximized) };
    }

    fn notify_tiled(&self) {
        // not supported
    }

    fn set_minimized(&self, minimized: bool) {
        unsafe { xwayland_view_minimize(self.c_ptr, minimized) };
    }

    fn configure(&self, geom: Rect, commit_move: *mut bool) {
        unsafe {
            xwayland_view_configure(self.c_ptr, geom, commit_move);
        }
    }

    fn adjust_usable_area(&self, output: *mut Output) {
        unsafe { xwayland_view_adjust_usable_area(self.c_ptr, output) };
    }

    fn get_focus_mode(&self) -> ViewFocusMode {
        unsafe { xwayland_view_get_focus_mode(self.c_ptr) }
    }

    fn offer_focus(&self) {
        unsafe { xwayland_view_offer_focus(self.c_ptr) };
    }

    fn close(&self) {
        unsafe { xwayland_view_close(self.c_ptr) };
    }

    fn adjust_scene_pos(&mut self, _state: &ViewState, x: i32, y: i32) -> (i32, i32) {
        (x, y)
    }
}

pub struct XdgView {
    c_ptr: *mut CView,
}

impl XdgView {
    pub fn new(c_ptr: *mut CView) -> Self {
        Self { c_ptr: c_ptr }
    }
}

impl ViewImpl for XdgView {
    fn get_surface(&self) -> *mut WlrSurface {
        unsafe { xdg_toplevel_view_get_surface(self.c_ptr) }
    }

    fn get_root_id(&self) -> ViewId {
        unsafe { xdg_toplevel_view_get_root_id(self.c_ptr) }
    }

    fn is_modal_dialog(&self) -> bool {
        unsafe { xdg_toplevel_view_is_modal_dialog(self.c_ptr) }
    }

    fn get_size_hints(&self) -> ViewSizeHints {
        unsafe { xdg_toplevel_view_get_size_hints(self.c_ptr) }
    }

    fn has_strut_partial(&self) -> bool {
        false
    }

    fn set_active(&self, active: bool) {
        unsafe { xdg_toplevel_view_set_active(self.c_ptr, active) };
    }

    fn set_fullscreen(&self, fullscreen: bool) {
        unsafe { xdg_toplevel_view_set_fullscreen(self.c_ptr, fullscreen) };
    }

    fn set_maximized(&self, maximized: ViewAxis) {
        unsafe { xdg_toplevel_view_maximize(self.c_ptr, maximized) };
    }

    fn notify_tiled(&self) {
        unsafe { xdg_toplevel_view_notify_tiled(self.c_ptr) };
    }

    fn set_minimized(&self, _minimized: bool) {
        // not supported
    }

    fn configure(&self, geom: Rect, commit_move: *mut bool) {
        unsafe {
            xdg_toplevel_view_configure(self.c_ptr, geom, commit_move);
        }
    }

    fn adjust_usable_area(&self, _output: *mut Output) {
        // not supported
    }

    fn get_focus_mode(&self) -> ViewFocusMode {
        VIEW_FOCUS_MODE_ALWAYS
    }

    fn offer_focus(&self) {
        // not supported
    }

    fn close(&self) {
        unsafe { xdg_toplevel_view_close(self.c_ptr) };
    }

    fn adjust_scene_pos(&mut self, state: &ViewState, x: i32, y: i32) -> (i32, i32) {
        if !state.fullscreen {
            return (x, y);
        }
        let output_geom = unsafe { output_layout_coords(state.output) };
        if rect_empty(output_geom) {
            unsafe { xdg_toplevel_view_disable_fullscreen_bg(self.c_ptr) };
            return (x, y);
        }
        // Center fullscreen views smaller than output and add black background
        let mut geom = rect_center(state.current.width, state.current.height, output_geom);
        rect_move_within(&mut geom, output_geom);
        if geom.width < output_geom.width || geom.height < output_geom.height {
            unsafe { xdg_toplevel_view_enable_fullscreen_bg(self.c_ptr, output_geom) };
        } else {
            unsafe { xdg_toplevel_view_disable_fullscreen_bg(self.c_ptr) };
        }
        return (geom.x, geom.y);
    }
}
