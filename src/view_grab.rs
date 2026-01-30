// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::rect::*;
use crate::view::*;
use std::cmp::max;

const SINGLE_AXIS_UNMAXIMIZE_THRESHOLD: i32 = 100;
const UNSNAP_THRESHOLD: i32 = 20;
const SNAP_THRESHOLD: i32 = 10;

#[derive(Default)]
pub struct ViewGrab {
    origin_cursor_x: i32,
    origin_cursor_y: i32,
    origin_geom: Rect,
    resize_edges: LabEdge,
}

// Adjusts one origin coordinate to account for resize during move
fn adjust_origin_pos(cursor_pos: i32, old_pos: i32, old_size: i32, new_size: i32) -> i32 {
    if old_size <= 0 {
        return old_pos; // defensive
    }
    let adjusted_pos = cursor_pos - ((cursor_pos - old_pos) * new_size / old_size);
    return max(old_pos, adjusted_pos);
}

// Overwrites x/y coordinates for any axis that should not unsnap
fn should_unsnap(state: &ViewState, x: &mut i32, y: &mut i32) -> bool {
    if state.floating() {
        return false;
    }
    let dx = (*x - state.pending.x).abs();
    let dy = (*y - state.pending.y).abs();
    if state.maximized == VIEW_AXIS_HORIZONTAL {
        if dx < SINGLE_AXIS_UNMAXIMIZE_THRESHOLD {
            *x = state.pending.x;
            return false;
        }
    } else if state.maximized == VIEW_AXIS_VERTICAL {
        if dy < SINGLE_AXIS_UNMAXIMIZE_THRESHOLD {
            *y = state.pending.y;
            return false;
        }
    } else {
        if (dx + dy) / 2 < UNSNAP_THRESHOLD {
            *x = state.pending.x;
            *y = state.pending.y;
            return false;
        }
    }
    return true;
}

impl ViewGrab {
    pub fn set_context(&mut self, view: &mut View, cursor_x: i32, cursor_y: i32, edges: LabEdge) {
        self.origin_cursor_x = cursor_x;
        self.origin_cursor_y = cursor_y;
        // Use current (visual) geometry as reference to compute deltas
        self.origin_geom = view.get_state().current;
        self.resize_edges = edges;
    }

    pub fn get_resize_edges(&self) -> LabEdge {
        self.resize_edges
    }

    pub fn start_move(&self, view: &mut View) -> bool {
        // Prevent moving panels etc. and fullscreen views
        if view.has_strut_partial() || view.get_state().fullscreen {
            return false;
        }
        // Store natural geometry at start of move
        view.store_natural_geom();
        return true;
    }

    // Adjusts origin geometry to account for resize during move
    pub fn adjust_move_origin(&mut self, width: i32, height: i32) {
        self.origin_geom.x = adjust_origin_pos(
            self.origin_cursor_x,
            self.origin_geom.x,
            self.origin_geom.width,
            width,
        );
        self.origin_geom.y = adjust_origin_pos(
            self.origin_cursor_y,
            self.origin_geom.y,
            self.origin_geom.height,
            height,
        );
        self.origin_geom.width = width;
        self.origin_geom.height = height;
    }

    pub fn compute_move_position(&self, cursor_x: i32, cursor_y: i32) -> (i32, i32) {
        let x = self.origin_geom.x + (cursor_x - self.origin_cursor_x);
        let y = self.origin_geom.y + (cursor_y - self.origin_cursor_y);
        return (x, y);
    }

    pub fn continue_move(&mut self, view: &mut View, cursor_x: i32, cursor_y: i32) {
        let state = view.get_state();
        let mut geom = state.pending;
        (geom.x, geom.y) = self.compute_move_position(cursor_x, cursor_y);
        // Unmaximize/untile view when cursor moves far enough
        if should_unsnap(state, &mut geom.x, &mut geom.y) {
            geom.width = state.natural_geom.width;
            geom.height = state.natural_geom.height;
            // Natural geometry may be 0x0 for xdg-shell view
            if !rect_empty(geom) {
                self.adjust_move_origin(geom.width, geom.height);
                (geom.x, geom.y) = self.compute_move_position(cursor_x, cursor_y);
            }
            view.set_maximized(VIEW_AXIS_NONE);
            view.set_tiled(LAB_EDGE_NONE);
        }
        view.move_resize(geom);
    }

    pub fn start_resize(&mut self, view: &mut View, edges: LabEdge) -> bool {
        let state = view.get_state();
        // Prevent resizing panels and fullscreen/fully maximized views
        if view.has_strut_partial() || state.fullscreen || state.maximized == VIEW_AXIS_BOTH {
            return false;
        }
        // Override resize_edges if specified explicitly
        if edges != LAB_EDGE_NONE {
            self.resize_edges = edges;
        }
        // At start of resize, unmaximize (in the axis/axes being resized)
        // and untile the view, but do not restore natural geometry
        let mut maximized = state.maximized;
        if (self.resize_edges & LAB_EDGES_LEFT_RIGHT) != 0 {
            maximized &= !VIEW_AXIS_HORIZONTAL;
        }
        if (self.resize_edges & LAB_EDGES_TOP_BOTTOM) != 0 {
            maximized &= !VIEW_AXIS_VERTICAL;
        }
        view.set_maximized(maximized);
        view.set_tiled(LAB_EDGE_NONE);
        return true;
    }

    pub fn continue_resize(&mut self, view: &mut View, cursor_x: i32, cursor_y: i32) {
        let mut geom = view.get_state().pending;
        let dx = cursor_x - self.origin_cursor_x;
        let dy = cursor_y - self.origin_cursor_y;
        // Compute new width/height based on cursor movement
        if (self.resize_edges & LAB_EDGE_TOP) != 0 {
            geom.height = self.origin_geom.height - dy;
        } else if (self.resize_edges & LAB_EDGE_BOTTOM) != 0 {
            geom.height = self.origin_geom.height + dy;
        }
        if (self.resize_edges & LAB_EDGE_LEFT) != 0 {
            geom.width = self.origin_geom.width - dx;
        } else if (self.resize_edges & LAB_EDGE_RIGHT) != 0 {
            geom.width = self.origin_geom.width + dx;
        }
        view.adjust_size(&mut geom.width, &mut geom.height);
        // Compute new position if resizing top/left edges
        if (self.resize_edges & LAB_EDGE_TOP) != 0 {
            geom.y = self.origin_geom.y + self.origin_geom.height - geom.height;
        }
        if (self.resize_edges & LAB_EDGE_LEFT) != 0 {
            geom.x = self.origin_geom.x + self.origin_geom.width - geom.width;
        }
        view.move_resize(geom);
    }
}

pub fn get_snap_target(cursor_x: i32, cursor_y: i32) -> (*mut Output, LabEdge) {
    let output = unsafe { output_nearest_to(cursor_x, cursor_y) };
    let usable = unsafe { output_usable_area_in_layout_coords(output) };
    if rect_empty(usable) {
        return (output, LAB_EDGE_NONE);
    }
    let mut edges = LAB_EDGE_NONE;
    if cursor_x < usable.x + SNAP_THRESHOLD {
        edges |= LAB_EDGE_LEFT;
    } else if cursor_x > usable.x + usable.width - SNAP_THRESHOLD {
        edges |= LAB_EDGE_RIGHT;
    }
    if cursor_y < usable.y + SNAP_THRESHOLD {
        edges |= LAB_EDGE_TOP;
    } else if cursor_y > usable.y + usable.height - SNAP_THRESHOLD {
        edges |= LAB_EDGE_BOTTOM;
    }
    return (output, edges);
}
