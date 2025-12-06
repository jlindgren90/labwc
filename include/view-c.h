/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_VIEW_C_H
#define LABWC_VIEW_C_H
/*
 * View functions implemented in C, called from Rust
 */

/* rust-friendly typedef */
typedef struct view CView;

void view_notify_app_id_change(CView *view);
void view_notify_title_change(CView *view);

#endif /* LABWC_VIEW_IMPL_H */
