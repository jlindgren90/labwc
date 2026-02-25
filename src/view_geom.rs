// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use crate::rect::*;
use std::cmp::{max, min};

pub const MIN_WIDTH: i32 = 100;
pub const MIN_HEIGHT: i32 = 60;
pub const MIN_VISIBLE_PX: i32 = 16;

pub fn compute_maximized_geom(state: &ViewState) -> Rect {
    let usable = unsafe { output_usable_area_in_layout_coords(state.output) };
    let margin = unsafe { ssd_get_margin(state) };
    let mut geom = rect_minus_margin(usable, margin);
    // If one axis (horizontal or vertical) is unmaximized, it should
    // use the natural geometry (first ensuring it is on-screen)
    if state.maximized != VIEW_AXIS_BOTH {
        let mut natural = state.natural_geom;
        ensure_geom_onscreen(state, &mut natural);
        if state.maximized == VIEW_AXIS_VERTICAL {
            geom.x = natural.x;
            geom.width = natural.width;
        } else if state.maximized == VIEW_AXIS_HORIZONTAL {
            geom.y = natural.y;
            geom.height = natural.height;
        }
    }
    return geom;
}

pub fn compute_tiled_geom(state: &ViewState) -> Rect {
    let usable = unsafe { output_usable_area_in_layout_coords(state.output) };
    let margin = unsafe { ssd_get_margin(state) };
    let (mut x1, mut x2) = (0, usable.width);
    let (mut y1, mut y2) = (0, usable.height);
    if (state.tiled & LAB_EDGE_RIGHT) != 0 {
        x1 = usable.width / 2;
    }
    if (state.tiled & LAB_EDGE_LEFT) != 0 {
        x2 = usable.width / 2;
    }
    if (state.tiled & LAB_EDGE_BOTTOM) != 0 {
        y1 = usable.height / 2;
    }
    if (state.tiled & LAB_EDGE_TOP) != 0 {
        y2 = usable.height / 2;
    }
    return Rect {
        x: usable.x + x1 + margin.left,
        y: usable.y + y1 + margin.top,
        width: x2 - x1 - (margin.left + margin.right),
        height: y2 - y1 - (margin.top + margin.bottom),
    };
}

pub fn ensure_geom_onscreen(state: &ViewState, geom: &mut Rect) {
    if rect_empty(*geom) {
        return;
    }
    let usable = unsafe { output_usable_area_in_layout_coords(state.output) };
    let margin = unsafe { ssd_get_margin(state) };
    let usable_minus_margin = rect_minus_margin(usable, margin);
    if rect_empty(usable_minus_margin) {
        return;
    }
    // Require a minimum number of pixels to be visible on each edge.
    // If the geometry minus this margin is offscreen, then center it.
    let hmargin = min(MIN_VISIBLE_PX, (geom.width - 1) / 2);
    let vmargin = min(MIN_VISIBLE_PX, (geom.height - 1) / 2);
    let reduced = Rect {
        x: geom.x + hmargin,
        y: geom.y + vmargin,
        width: geom.width - 2 * hmargin,
        height: geom.height - 2 * vmargin,
    };
    if !rect_intersects(reduced, usable) {
        *geom = rect_center(geom.width, geom.height, usable_minus_margin);
        rect_move_within(geom, usable_minus_margin);
    }
}

pub fn nearest_output_to_geom(geom: Rect) -> *mut Output {
    return unsafe { output_nearest_to(geom.x + geom.width / 2, geom.y + geom.height / 2) };
}

// Note: rel_to and keep_position are mutually exclusive
pub fn compute_default_geom(state: &ViewState, geom: &mut Rect, rel_to: Rect, keep_position: bool) {
    let margin = unsafe { ssd_get_margin(state) };
    if rect_empty(*geom) {
        // Invalid size - just ensure top-left corner is non-negative
        geom.x = max(geom.x, margin.left);
        geom.y = max(geom.y, margin.top);
        return;
    }
    let usable = unsafe { output_usable_area_in_layout_coords(state.output) };
    let usable_minus_margin = rect_minus_margin(usable, margin);
    let rel_to_minus_margin = rect_minus_margin(rel_to, margin);
    if rect_empty(usable_minus_margin) {
        // Invalid output geometry - center to parent if possible,
        // then ensure top-left corner is non-negative
        if !rect_empty(rel_to_minus_margin) {
            *geom = rect_center(geom.width, geom.height, rel_to_minus_margin);
        }
        geom.x = max(geom.x, margin.left);
        geom.y = max(geom.y, margin.top);
        return;
    }
    // Limit size (including margins) to usable area
    geom.width = min(geom.width, usable_minus_margin.width);
    geom.height = min(geom.height, usable_minus_margin.height);
    // Center as requested
    if !rect_empty(rel_to_minus_margin) {
        *geom = rect_center(geom.width, geom.height, rel_to_minus_margin);
    } else if !keep_position {
        *geom = rect_center(geom.width, geom.height, usable_minus_margin);
    }
    // Finally, move within usable area
    rect_move_within(geom, usable_minus_margin);
}
