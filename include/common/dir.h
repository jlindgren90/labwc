/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_DIR_H
#define LABWC_DIR_H

#include <vector>
#include "common/str.h"

std::vector<lab_str> paths_config_create(const char *filename);
std::vector<lab_str> paths_theme_create(const char *theme_name,
	const char *filename);

#endif /* LABWC_DIR_H */
