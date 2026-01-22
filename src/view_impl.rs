// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;

pub trait ViewImpl {
    fn get_root_id(&self) -> ViewId;
    fn is_modal_dialog(&self) -> bool;
    fn set_active(&self, active: bool);
    fn set_fullscreen(&self, fullscreen: bool);
    fn set_maximized(&self, maximized: ViewAxis);
    fn notify_tiled(&self);
    fn set_minimized(&self, minimized: bool);
    fn configure(&self, geom: Rect, pending: *mut Rect, current: *mut Rect);
    fn get_focus_mode(&self) -> ViewFocusMode;
    fn focus(&self) -> bool;
    fn offer_focus(&self);
    fn close(&self);
}

pub struct XView {
    c_ptr: *mut CView,
}

impl XView {
    pub fn new(c_ptr: *mut CView) -> Self {
        Self { c_ptr }
    }

    fn get_surface(&self) -> *mut WlrSurface {
        unsafe { xwayland_view_get_surface(self.c_ptr) }
    }
}

impl ViewImpl for XView {
    fn get_root_id(&self) -> ViewId {
        unsafe { xwayland_view_get_root_id(self.c_ptr) }
    }

    fn is_modal_dialog(&self) -> bool {
        unsafe { xwayland_view_is_modal_dialog(self.c_ptr) }
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

    fn configure(&self, geom: Rect, pending: *mut Rect, current: *mut Rect) {
        unsafe {
            xwayland_view_configure(self.c_ptr, geom, pending, current);
        }
    }

    fn get_focus_mode(&self) -> ViewFocusMode {
        unsafe { xwayland_view_get_focus_mode(self.c_ptr) }
    }

    fn focus(&self) -> bool {
        unsafe { seat_focus_surface_no_notify(self.get_surface()) }
    }

    fn offer_focus(&self) {
        unsafe { xwayland_view_offer_focus(self.c_ptr) };
    }

    fn close(&self) {
        unsafe { xwayland_view_close(self.c_ptr) };
    }
}

pub struct XdgView {
    c_ptr: *mut CView,
}

impl XdgView {
    pub fn new(c_ptr: *mut CView) -> Self {
        Self { c_ptr: c_ptr }
    }

    fn get_surface(&self) -> *mut WlrSurface {
        unsafe { xdg_toplevel_view_get_surface(self.c_ptr) }
    }
}

impl ViewImpl for XdgView {
    fn get_root_id(&self) -> ViewId {
        unsafe { xdg_toplevel_view_get_root_id(self.c_ptr) }
    }

    fn is_modal_dialog(&self) -> bool {
        unsafe { xdg_toplevel_view_is_modal_dialog(self.c_ptr) }
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

    fn configure(&self, geom: Rect, pending: *mut Rect, current: *mut Rect) {
        unsafe {
            xdg_toplevel_view_configure(self.c_ptr, geom, pending, current);
        }
    }

    fn get_focus_mode(&self) -> ViewFocusMode {
        VIEW_FOCUS_MODE_ALWAYS
    }

    fn focus(&self) -> bool {
        unsafe { seat_focus_surface_no_notify(self.get_surface()) }
    }

    fn offer_focus(&self) {
        // not supported
    }

    fn close(&self) {
        unsafe { xdg_toplevel_view_close(self.c_ptr) };
    }
}
