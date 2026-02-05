// SPDX-License-Identifier: GPL-2.0-only
//
// This header is processed to produce Rust bindings for functions
// implemented in C. When adding #includes here, also add them (and
// any indirect #includes) as inputs for bindings.rs in meson.build.
//
#ifndef LABWC_RS_BINDINGS_H
#define LABWC_RS_BINDINGS_H

#include "foreign-toplevel.h"
#include "ssd.h"
#include "view-c.h"

// minimal cairo bindings
int cairo_image_surface_get_width(CairoSurface *surface);
int cairo_image_surface_get_height(CairoSurface *surface);
void cairo_surface_destroy(CairoSurface *surface);

// minimal wlroots bindings
void wlr_buffer_drop(WlrBuffer *buffer);

#endif // LABWC_RS_BINDINGS_H
