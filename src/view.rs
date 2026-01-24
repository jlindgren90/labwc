// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::foreign_toplevel::*;
use crate::rect::*;
use crate::view_impl::*;
use std::ffi::CString;

impl ViewState {
    pub fn focusable(&self) -> bool {
        self.mapped
            && (self.focus_mode == VIEW_FOCUS_MODE_ALWAYS
                || self.focus_mode == VIEW_FOCUS_MODE_LIKELY)
    }

    pub fn floating(&self) -> bool {
        !self.fullscreen && self.maximized == VIEW_AXIS_NONE && self.tiled == LAB_EDGE_NONE
    }
}

impl From<&ViewState> for ForeignToplevelState {
    fn from(state: &ViewState) -> Self {
        ForeignToplevelState {
            maximized: state.maximized == VIEW_AXIS_BOTH,
            minimized: state.minimized,
            activated: state.active,
            fullscreen: state.fullscreen,
        }
    }
}

fn nearest_output_to_geom(geom: Rect) -> *mut Output {
    return unsafe { output_nearest_to(geom.x + geom.width / 2, geom.y + geom.height / 2) };
}

#[derive(Default)]
struct ViewData {
    app_id: CString,
    title: CString,
    foreign_toplevels: Vec<ForeignToplevel>,
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
            for toplevel in &self.d.foreign_toplevels {
                toplevel.send_app_id(&self.d.app_id);
                toplevel.send_done();
            }
        }
    }

    pub fn set_title(&mut self, title: CString) {
        if self.d.title != title {
            self.d.title = title;
            self.state.title = self.d.title.as_ptr(); // for C interop
            unsafe { view_notify_title_change(self.c_ptr) };
            for toplevel in &self.d.foreign_toplevels {
                toplevel.send_title(&self.d.title);
                toplevel.send_done();
            }
        }
    }

    // Returns CView pointer to pass to view_notify_map()
    pub fn set_mapped(&mut self) -> *mut CView {
        self.state.mapped = true;
        self.state.ever_mapped = true;
        self.state.focus_mode = self.v.get_focus_mode();
        return self.c_ptr;
    }

    // Returns CView pointer to pass to view_notify_unmap()
    pub fn set_unmapped(&mut self) -> *mut CView {
        self.state.mapped = false;
        for resource in self.d.foreign_toplevels.drain(..) {
            resource.close();
        }
        return self.c_ptr;
    }

    pub fn set_active(&mut self, active: bool) {
        if self.state.active != active {
            self.state.active = active;
            self.v.set_active(active);
            unsafe { view_notify_active(self.c_ptr) };
            self.send_foreign_toplevel_state();
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
            self.send_foreign_toplevel_state();
        }
    }

    pub fn set_maximized(&mut self, maximized: ViewAxis) {
        if self.state.maximized != maximized {
            self.state.maximized = maximized;
            self.v.set_maximized(maximized);
            self.send_foreign_toplevel_state();
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
            self.send_foreign_toplevel_state();
        }
    }

    pub fn set_current_pos(&mut self, x: i32, y: i32) {
        self.state.current.x = x;
        self.state.current.y = y;
    }

    pub fn set_current_size(&mut self, width: i32, height: i32) {
        self.state.current.width = width;
        self.state.current.height = height;
    }

    pub fn set_pending_geom(&mut self, geom: Rect) {
        self.state.pending = geom;
    }

    pub fn move_resize(&mut self, geom: Rect) {
        if rect_equals(self.state.pending, geom) {
            return;
        }
        self.v
            .configure(geom, &mut self.state.pending, &mut self.state.current);
        if self.state.floating() {
            // Moving a floating view also sets the output
            self.state.output = nearest_output_to_geom(self.state.pending);
        }
        unsafe { view_notify_move_resize(self.c_ptr) };
    }

    pub fn set_natural_geom(&mut self, geom: Rect) {
        self.state.natural_geom = geom;
    }

    pub fn store_natural_geom(&mut self) {
        // Don't save natural geometry if fullscreen or tiled
        if self.state.fullscreen || self.state.tiled != 0 {
            return;
        }
        // If only one axis is maximized, save geometry of the other
        if self.state.maximized == VIEW_AXIS_NONE || self.state.maximized == VIEW_AXIS_VERTICAL {
            self.state.natural_geom.x = self.state.pending.x;
            self.state.natural_geom.width = self.state.pending.width;
        }
        if self.state.maximized == VIEW_AXIS_NONE || self.state.maximized == VIEW_AXIS_HORIZONTAL {
            self.state.natural_geom.y = self.state.pending.y;
            self.state.natural_geom.height = self.state.pending.height;
        }
    }

    pub fn set_output(&mut self, output: *mut Output) {
        self.state.output = output;
    }

    pub fn offer_focus(&self) {
        self.v.offer_focus();
    }

    pub fn add_foreign_toplevel(&mut self, client: *mut WlResource) {
        let toplevel = ForeignToplevel::new(client, self.c_ptr);
        toplevel.send_app_id(&self.d.app_id);
        toplevel.send_title(&self.d.title);
        toplevel.send_state((&*self.state).into());
        toplevel.send_done();
        self.d.foreign_toplevels.push(toplevel);
    }

    pub fn remove_foreign_toplevel(&mut self, resource: *mut WlResource) {
        self.d.foreign_toplevels.retain(|t| t.res != resource);
    }

    fn send_foreign_toplevel_state(&self) {
        for toplevel in &self.d.foreign_toplevels {
            toplevel.send_state((&*self.state).into());
            toplevel.send_done();
        }
    }
}
