// SPDX-License-Identifier: GPL-2.0-only
//
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
