// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use std::collections::HashMap;
use std::ptr::null_mut;

#[derive(Default)]
struct XSurfaceData {
    xsurface: *mut XSurface,
    serial: u64,
    surface_id: u32,
    view_id: ViewId,
    parent_xid: XId,
    unmanaged_node: *mut WlrSceneNode,
}

#[derive(Default)]
pub struct Xwm {
    surfaces: HashMap<XId, XSurfaceData>,
    // xwayland server-side stacking order, back-to-front, with
    // minimized views first and always-on-top views last
    stacking: Vec<XId>,
}

impl Xwm {
    pub fn add(&mut self, xid: XId, xsurface: *mut XSurface) {
        let mut surf = XSurfaceData::default();
        surf.xsurface = xsurface;
        self.surfaces.insert(xid, surf);
    }

    // returns None for unmanaged surface
    pub fn get_view_id(&self, xid: XId) -> Option<ViewId> {
        self.surfaces
            .get(&xid)
            .map(|s| s.view_id)
            .filter(|&i| i != 0)
    }

    pub fn get_xsurface(&self, xid: XId) -> *mut XSurface {
        self.surfaces.get(&xid).map_or(null_mut(), |s| s.xsurface)
    }

    pub fn get_for_serial(&self, serial: u64) -> *mut XSurface {
        self.surfaces
            .values()
            .find(|s| s.serial == serial)
            .map_or(null_mut(), |s| s.xsurface)
    }

    pub fn get_for_surface_id(&self, surface_id: u32) -> *mut XSurface {
        self.surfaces
            .values()
            .find(|s| s.surface_id == surface_id)
            .map_or(null_mut(), |s| s.xsurface)
    }

    pub fn get_all(&self) -> Vec<*mut XSurface> {
        self.surfaces.values().map(|s| s.xsurface).collect()
    }

    pub fn remove(&mut self, xid: XId) {
        self.surfaces.remove(&xid);
    }

    pub fn set_parent_xid(&mut self, xid: XId, parent_xid: XId) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.parent_xid = parent_xid;
        }
    }

    pub fn set_serial(&mut self, xid: XId, serial: u64) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.serial = serial;
        }
    }

    pub fn set_surface_id(&mut self, xid: XId, surface_id: u32) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.surface_id = surface_id;
        }
    }

    pub fn set_view_id(&mut self, xid: XId, view_id: ViewId) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            surf.view_id = view_id;
            unsafe { xwayland_surface_set_view_id(surf.xsurface, view_id) };
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
        while surf.parent_xid != 0
            && let Some(parent) = self.surfaces.get(&surf.parent_xid)
            && parent.view_id != 0
            && parent.view_id < surf.view_id
        {
            surf = parent;
        }
        return surf.view_id;
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

    pub fn lower(&mut self, xid: XId, xsurface: *mut XSurface) {
        unsafe { xwayland_surface_stack_above(xsurface, 0) }; // None
        self.stacking.retain(|&x| x != xid);
        self.stacking.insert(0, xid);
    }

    pub fn map_unmanaged(&mut self, xid: XId, surface: *mut WlrSurface) {
        if let Some(surf) = self.surfaces.get_mut(&xid) {
            if surf.unmanaged_node.is_null() {
                surf.unmanaged_node = unsafe { xwayland_create_unmanaged_node(surface) };
            }
            let props = unsafe { xwayland_surface_get_props(surf.xsurface) };
            unsafe { wlr_scene_node_set_position(surf.unmanaged_node, props.geom.x, props.geom.y) };
        }
    }

    pub fn unmap_unmanaged(&mut self, xid: XId) {
        if let Some(surf) = self.surfaces.get_mut(&xid)
            && !surf.unmanaged_node.is_null()
        {
            unsafe { wlr_scene_node_destroy(surf.unmanaged_node) };
            surf.unmanaged_node = null_mut();
        }
    }

    pub fn move_unmanaged(&self, xid: XId, x: i32, y: i32) {
        if let Some(surf) = self.surfaces.get(&xid)
            && !surf.unmanaged_node.is_null()
        {
            unsafe { wlr_scene_node_set_position(surf.unmanaged_node, x, y) };
        }
    }
}
