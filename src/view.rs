// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use std::ffi::CString;

#[derive(Default)]
pub struct View {
    c_ptr: *mut CView,
    app_id: CString,
    title: CString,
    state: Box<ViewState>,
}

impl View {
    pub fn new(c_ptr: *mut CView) -> Self {
        let mut view = View {
            c_ptr: c_ptr,
            ..View::default()
        };
        view.state.app_id = view.app_id.as_ptr(); // for C interop
        view.state.title = view.title.as_ptr(); // for C interop
        return view;
    }

    pub fn get_state(&self) -> &ViewState {
        &self.state
    }

    pub fn set_app_id(&mut self, app_id: CString) {
        if self.app_id != app_id {
            self.app_id = app_id;
            self.state.app_id = self.app_id.as_ptr(); // for C interop
            unsafe { view_notify_app_id_change(self.c_ptr) };
        }
    }

    pub fn set_title(&mut self, title: CString) {
        if self.title != title {
            self.title = title;
            self.state.title = self.title.as_ptr(); // for C interop
            unsafe { view_notify_title_change(self.c_ptr) };
        }
    }
}
