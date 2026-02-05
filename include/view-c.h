// SPDX-License-Identifier: GPL-2.0-only
//
// View-related functions implemented in C, callable from Rust
//
#ifndef LABWC_VIEW_C_H
#define LABWC_VIEW_C_H

#include "rs-types.h"

typedef struct ViewState {
	const char *app_id;
	const char *title;
} ViewState;

void view_notify_app_id_change(CView *view);
void view_notify_title_change(CView *view);

#endif // LABWC_VIEW_C_H
