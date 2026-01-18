// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use std::ffi::{c_char, CStr, CString};

pub fn cstring(s: *const c_char) -> CString {
    if s.is_null() {
        CString::default()
    } else {
        // SAFETY: requires valid C string
        unsafe { CStr::from_ptr(s).to_owned() }
    }
}

#[macro_export]
macro_rules! lazy_static {
    ($name:ident, $type:ty, $init:expr, $read:ident, $write:ident) => {
        static mut $name: Option<std::cell::RefCell<$type>> = None;

        fn $read() -> std::cell::Ref<'static, $type> {
            // SAFETY: single-threaded
            unsafe {
                #[allow(static_mut_refs)]
                $name.get_or_insert_with(|| $init.into()).borrow()
            }
        }

        fn $write() -> std::cell::RefMut<'static, $type> {
            // SAFETY: single-threaded
            unsafe {
                #[allow(static_mut_refs)]
                $name.get_or_insert_with(|| $init.into()).borrow_mut()
            }
        }
    };
}

#[no_mangle]
pub extern "C" fn rect_empty(rect: Rect) -> bool {
    return rect.width <= 0 || rect.height <= 0;
}

#[no_mangle]
pub extern "C" fn rect_intersects(a: Rect, b: Rect) -> bool {
    if rect_empty(a) || rect_empty(b) {
        return false;
    }
    return a.x < b.x + b.width
        && b.x < a.x + a.width
        && a.y < b.y + b.height
        && b.y < a.y + a.height;
}

#[no_mangle]
pub extern "C" fn rect_center(width: i32, height: i32, rel_to: Rect, bound: Rect) -> Rect {
    // Compute centered position
    let mut x = rel_to.x + (rel_to.width - width) / 2;
    let mut y = rel_to.y + (rel_to.height - height) / 2;
    // Limit within bounding rect if possible
    if x < bound.x {
        x = bound.x;
    } else if x + width > bound.x + bound.width {
        x = bound.x + bound.width - width;
    }
    if y < bound.y {
        y = bound.y;
    } else if y + height > bound.y + bound.height {
        y = bound.y + bound.height - height;
    }
    return Rect {
        x: x,
        y: y,
        width: width,
        height: height,
    };
}

#[no_mangle]
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
    // Compute centered position
    return Rect {
        x: bound.x + (bound.width - w) / 2,
        y: bound.y + (bound.height - h) / 2,
        width: w,
        height: h,
    };
}
