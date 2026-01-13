// SPDX-License-Identifier: GPL-2.0-only
//
// Top-level Rust module which pulls in all other .rs sources
//
#[allow(dead_code)]
mod bindings {
    include!("../build/include/bindings.rs");
}
mod foreign_toplevel;
mod util;
mod view;
mod view_api;
mod views;
