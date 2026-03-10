// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;

#[derive(Default)]
pub struct Xwm {
    // xwayland server-side stacking order, back-to-front, with
    // minimized views first and always-on-top views last
    stacking: Vec<XId>,
}

impl Xwm {
    pub fn raise(&mut self, xid: XId, xsurface: *mut XSurface) {
        if let Some(&prev) = self.stacking.last()
            && prev != xid
        {
            unsafe { xwayland_surface_stack_above(xsurface, prev) };
        }
        self.stacking.retain(|&x| x != xid);
        self.stacking.push(xid);
    }

    pub fn set_minimized(&mut self, xid: XId, xsurface: *mut XSurface, state: &ViewState) {
        unsafe { xwayland_surface_publish_state(xsurface, state) };
        if state.minimized {
            // restack minimized view to bottom
            unsafe {
                xwayland_surface_stack_above(xsurface, 0 /* None */)
            };
            self.stacking.retain(|&x| x != xid);
            self.stacking.insert(0, xid);
        }
    }

    pub fn unstack(&mut self, xid: XId) {
        self.stacking.retain(|&x| x != xid);
    }
}
