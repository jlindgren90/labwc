// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::view_impl::*;
use std::ffi::CString;

impl ViewState {
    pub fn floating(&self) -> bool {
        !self.fullscreen && self.maximized == VIEW_AXIS_NONE && self.tiled == LAB_EDGE_NONE
    }
}

#[derive(Default)]
struct ViewData {
    app_id: CString,
    title: CString,
}

pub struct View {
    v: Box<dyn ViewImpl>,
    d: ViewData,
    state: Box<ViewState>,
    c_ptr: *mut CView, // TODO: remove
}

impl View {
    pub fn new(c_ptr: *mut CView, is_xwayland: bool) -> Self {
        let mut view = View {
            v: if is_xwayland {
                Box::new(XView::new(c_ptr))
            } else {
                Box::new(XdgView::new(c_ptr))
            },
            d: ViewData::default(),
            state: Box::default(),
            c_ptr: c_ptr,
        };
        view.state.app_id = view.d.app_id.as_ptr(); // for C interop
        view.state.title = view.d.title.as_ptr(); // for C interop
        return view;
    }

    pub fn get_state(&self) -> &ViewState {
        &self.state
    }

    pub fn set_app_id(&mut self, app_id: CString) {
        if self.d.app_id != app_id {
            self.d.app_id = app_id;
            self.state.app_id = self.d.app_id.as_ptr(); // for C interop
            unsafe { view_notify_app_id_change(self.c_ptr) };
        }
    }

    pub fn set_title(&mut self, title: CString) {
        if self.d.title != title {
            self.d.title = title;
            self.state.title = self.d.title.as_ptr(); // for C interop
            unsafe { view_notify_title_change(self.c_ptr) };
        }
    }

    // Returns CView pointer to pass to view_notify_map()
    pub fn set_mapped(&mut self) -> *mut CView {
        self.state.mapped = true;
        self.state.ever_mapped = true;
        return self.c_ptr;
    }

    // Returns CView pointer to pass to view_notify_unmap()
    pub fn set_unmapped(&mut self) -> *mut CView {
        self.state.mapped = false;
        return self.c_ptr;
    }

    pub fn set_active(&mut self, active: bool) {
        if self.state.active != active {
            self.state.active = active;
            self.v.set_active(active);
            unsafe { view_notify_active(self.c_ptr) };
        }
    }

    pub fn set_ssd_enabled(&mut self, ssd_enabled: bool) {
        if self.state.ssd_enabled != ssd_enabled {
            self.state.ssd_enabled = ssd_enabled;
            unsafe { view_notify_ssd_enabled(self.c_ptr) };
        }
    }

    pub fn set_fullscreen(&mut self, fullscreen: bool) {
        if self.state.fullscreen != fullscreen {
            self.state.fullscreen = fullscreen;
            self.v.set_fullscreen(fullscreen);
        }
    }

    pub fn set_maximized(&mut self, maximized: ViewAxis) {
        if self.state.maximized != maximized {
            self.state.maximized = maximized;
            self.v.set_maximized(maximized);
            unsafe { view_notify_maximized(self.c_ptr) };
        }
    }

    pub fn set_tiled(&mut self, tiled: LabEdge) {
        if self.state.tiled != tiled {
            self.state.tiled = tiled;
            self.v.notify_tiled();
        }
    }

    pub fn set_minimized(&mut self, minimized: bool) {
        if self.state.minimized != minimized {
            self.state.minimized = minimized;
            self.v.set_minimized(minimized);
        }
    }
}
