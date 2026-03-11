// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;

#[unsafe(no_mangle)]
pub extern "C" fn rect_empty(rect: Rect) -> bool {
    rect.width <= 0 || rect.height <= 0
}

#[unsafe(no_mangle)]
pub extern "C" fn rect_equals(a: Rect, b: Rect) -> bool {
    a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height
}

#[unsafe(no_mangle)]
pub extern "C" fn rect_intersects(a: Rect, b: Rect) -> bool {
    !rect_empty(a)
        && !rect_empty(b)
        && a.x < b.x + b.width
        && b.x < a.x + a.width
        && a.y < b.y + b.height
        && b.y < a.y + a.height
}

#[unsafe(no_mangle)]
pub extern "C" fn rect_center(width: i32, height: i32, rel_to: Rect) -> Rect {
    Rect {
        x: rel_to.x + (rel_to.width - width) / 2,
        y: rel_to.y + (rel_to.height - height) / 2,
        width: width,
        height: height,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rect_minus_margin(rect: Rect, margin: Border) -> Rect {
    Rect {
        x: rect.x + margin.left,
        y: rect.y + margin.top,
        width: rect.width - margin.left - margin.right,
        height: rect.height - margin.top - margin.bottom,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rect_move_within(rect: &mut Rect, bound: Rect) {
    if rect.x < bound.x {
        rect.x = bound.x;
    } else if rect.x + rect.width > bound.x + bound.width {
        rect.x = bound.x + bound.width - rect.width;
    }
    if rect.y < bound.y {
        rect.y = bound.y;
    } else if rect.y + rect.height > bound.y + bound.height {
        rect.y = bound.y + bound.height - rect.height;
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rect_fit_within(width: i32, height: i32, bound: Rect) -> Rect {
    let (w, h);
    if width <= bound.width && height <= bound.height {
        // No downscaling needed
        w = width;
        h = height;
    } else if width * bound.height > height * bound.width {
        // Wider content, fit width
        w = bound.width;
        h = (height * bound.width + (width / 2)) / width;
    } else {
        // Taller content, fit height
        w = (width * bound.height + (height / 2)) / height;
        h = bound.height;
    }
    return rect_center(w, h, bound);
}
