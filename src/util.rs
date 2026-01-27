// SPDX-License-Identifier: GPL-2.0-only
//
use crate::bindings::*;
use std::ffi::{CStr, CString, c_char};

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

pub struct CairoSurfacePtr {
    pub surface: *mut CairoSurface,
}

impl CairoSurfacePtr {
    pub fn new(surface: *mut CairoSurface) -> Self {
        Self { surface: surface }
    }
}

impl Drop for CairoSurfacePtr {
    fn drop(&mut self) {
        unsafe { cairo_surface_destroy(self.surface) };
    }
}

pub struct WlrBufferPtr {
    pub buffer: *mut WlrBuffer,
}

impl WlrBufferPtr {
    pub fn new(buffer: *mut WlrBuffer) -> Self {
        Self { buffer: buffer }
    }
}

impl Drop for WlrBufferPtr {
    fn drop(&mut self) {
        unsafe { wlr_buffer_drop(self.buffer) };
    }
}
