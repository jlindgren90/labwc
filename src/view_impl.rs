// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;

pub trait ViewImpl {
    fn set_active(&self, active: bool);
    fn set_fullscreen(&self, fullscreen: bool);
    fn set_maximized(&self, maximized: ViewAxis);
    fn notify_tiled(&self);
    fn set_minimized(&self, minimized: bool);
    fn get_focus_mode(&self) -> ViewFocusMode;
    fn offer_focus(&self);
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

    fn get_focus_mode(&self) -> ViewFocusMode {
        unsafe { xwayland_view_get_focus_mode(self.c_ptr) }
    }

    fn offer_focus(&self) {
        unsafe { xwayland_view_offer_focus(self.c_ptr) };
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

    fn get_focus_mode(&self) -> ViewFocusMode {
        VIEW_FOCUS_MODE_ALWAYS
    }

    fn offer_focus(&self) {
        // not supported
    }
}
