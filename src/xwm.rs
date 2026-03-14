// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use std::collections::HashMap;
use std::ptr::null_mut;

#[derive(Default)]
struct XSurfaceData {
    // boxed because XSurface points back to XSurfaceInfo
    info: Box<XSurfaceInfo>,
}

#[derive(Default)]
pub struct Xwm {
    surfaces: HashMap<XId, XSurfaceData>,
    // xwayland server-side stacking order, back-to-front, with
    // minimized views first and always-on-top views last
    stacking: Vec<XId>,
}

impl Xwm {
    pub fn add(&mut self, xid: XId, xsurface: *mut XSurface) -> &XSurfaceInfo {
        let mut surf = XSurfaceData::default();
        surf.info.xsurface = xsurface;
        return &*self.surfaces.entry(xid).insert_entry(surf).into_mut().info;
    }

    pub fn get_info(&self, xid: XId) -> Option<&XSurfaceInfo> {
        self.surfaces.get(&xid).map(|s| &*s.info)
    }

    pub fn get_for_serial(&self, serial: u64) -> *mut XSurface {
        self.surfaces
            .values()
            .find(|s| s.info.serial == serial)
            .map_or(null_mut(), |s| s.info.xsurface)
    }

    pub fn get_for_surface_id(&self, surface_id: u32) -> *mut XSurface {
        self.surfaces
            .values()
            .find(|s| s.info.surface_id == surface_id)
            .map_or(null_mut(), |s| s.info.xsurface)
    }

    pub fn get_all(&self) -> Vec<*mut XSurface> {
        self.surfaces.values().map(|s| s.info.xsurface).collect()
    }

    pub fn remove(&mut self, xid: XId) {
        self.surfaces.remove(&xid);
    }

    pub fn set_parent_xid(&mut self, xid: XId, parent_xid: XId) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.info.parent_xid = parent_xid;
        }
    }

    pub fn set_serial(&mut self, xid: XId, serial: u64) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.info.serial = serial;
        }
    }

    pub fn set_surface_id(&mut self, xid: XId, surface_id: u32) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.info.surface_id = surface_id;
        }
    }

    pub fn set_view_id(&mut self, xid: XId, view_id: ViewId) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.info.view_id = view_id;
        }
        // Remove from stacking order if no longer a view
        if view_id == 0 {
            self.stacking.retain(|&x| x != xid);
        }
    }

    pub fn get_root_view_id(&self, xid: XId) -> ViewId {
        let Some(mut surf) = self.surfaces.get(&xid) else {
            return 0;
        };
        // Walk up the surface tree. To prevent an infinite loop,
        // only follow a parent if it was created before its child.
        while surf.info.parent_xid != 0
            && let Some(parent) = self.surfaces.get(&surf.info.parent_xid)
            && parent.info.view_id != 0
            && parent.info.view_id < surf.info.view_id
        {
            surf = parent;
        }
        return surf.info.view_id;
    }

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
}
