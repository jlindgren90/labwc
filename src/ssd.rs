// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use std::ptr::null_mut;

#[derive(Default)]
pub struct Ssd {
    pub c_ptr: *mut CSsd,
}

impl Ssd {
    pub fn create(&mut self, view_ptr: *mut CView, icon_buffer: *mut WlrBuffer) {
        if self.c_ptr.is_null() {
            self.c_ptr = unsafe { ssd_create(view_ptr, icon_buffer) };
        }
    }

    pub fn destroy(&mut self) {
        unsafe { ssd_destroy(self.c_ptr) };
        self.c_ptr = null_mut();
    }

    pub fn set_active(&mut self, active: bool) {
        unsafe { ssd_set_active(self.c_ptr, active) };
    }

    pub fn set_inhibits_keybinds(&mut self, inhibits_keybinds: bool) {
        unsafe { ssd_enable_keybind_inhibit_indicator(self.c_ptr, inhibits_keybinds) };
    }

    pub fn update_geom(&mut self) {
        unsafe { ssd_update_geometry(self.c_ptr) };
    }

    pub fn update_icon(&mut self, icon_buffer: *mut WlrBuffer) {
        unsafe { ssd_update_icon(self.c_ptr, icon_buffer) };
    }

    pub fn update_title(&mut self) {
        unsafe { ssd_update_title(self.c_ptr) };
    }
}
