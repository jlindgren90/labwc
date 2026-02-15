/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SCALED_ICON_BUFFER_H
#define LABWC_SCALED_ICON_BUFFER_H

struct view;

struct lab_data_buffer *scaled_icon_buffer_load(struct view *view,
	int icon_size);

#endif /* LABWC_SCALED_ICON_BUFFER_H */
