// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use std::ffi::CString;

pub struct ForeignToplevel {
    pub res: *mut WlResource,
}

impl ForeignToplevel {
    pub fn new(client: *mut WlResource, view_id: ViewId) -> Self {
        Self {
            res: unsafe { foreign_toplevel_create(client, view_id) },
        }
    }

    pub fn send_app_id(&self, app_id: &CString) {
        unsafe { foreign_toplevel_send_app_id(self.res, app_id.as_ptr()) };
    }

    pub fn send_title(&self, title: &CString) {
        unsafe { foreign_toplevel_send_title(self.res, title.as_ptr()) };
    }

    pub fn send_state(&self, state: ForeignToplevelState) {
        unsafe { foreign_toplevel_send_state(self.res, state) };
    }

    pub fn send_done(&self) {
        unsafe { foreign_toplevel_send_done(self.res) };
    }

    pub fn close(&self) {
        unsafe { foreign_toplevel_close(self.res) };
    }
}
