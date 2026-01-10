use bindings::*;
use std::ffi::{c_char, CStr, CString};

#[no_mangle]
pub extern "C" fn box_empty(b: WlrBox) -> bool {
    return b.width <= 0 || b.height <= 0;
}

#[no_mangle]
pub extern "C" fn box_intersects(a: WlrBox, b: WlrBox) -> bool {
    if box_empty(a) || box_empty(b) {
        return false;
    }
    return a.x < b.x + b.width
        && b.x < a.x + a.width
        && a.y < b.y + b.height
        && b.y < a.y + a.height;
}

// Centers a content box (width & height) within a reference box,
// limiting it (if possible) to not extend outside a bounding box.
//
// The reference box and bounding box are often the same but could be
// different (e.g. when centering a view within its parent but limiting
// to usable output area).
//
#[no_mangle]
pub extern "C" fn box_center(
    width: i32,
    height: i32,
    rel_to: WlrBox,
    bound: WlrBox,
    x: &mut i32,
    y: &mut i32,
) {
    *x = rel_to.x + (rel_to.width - width) / 2;
    *y = rel_to.y + (rel_to.height - height) / 2;

    if *x < bound.x {
        *x = bound.x;
    } else if *x + width > bound.x + bound.width {
        *x = bound.x + bound.width - width;
    }
    if *y < bound.y {
        *y = bound.y;
    } else if *y + height > bound.y + bound.height {
        *y = bound.y + bound.height - height;
    }
}

// Fits and centers a content box (width & height) within a bounding box.
// The content box is downscaled if necessary (preserving aspect ratio)
// but not upscaled.
//
// The returned x & y coordinates are the centered content position
// relative to the top-left corner of the bounding box.
//
#[no_mangle]
pub extern "C" fn box_fit_within(width: i32, height: i32, bound: WlrBox) -> WlrBox {
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
    return WlrBox {
        x: bound.x + (bound.width - w) / 2,
        y: bound.y + (bound.height - h) / 2,
        width: w,
        height: h,
    };
}

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
