// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "config/touch.h"
#include <strings.h>
#include <wlr/util/log.h>
#include "config/rcxml.h"

static struct touch_config_entry *
find_default_config(void)
{
	for (auto iter = rc.touch_configs.rbegin(),
			end = rc.touch_configs.rend();
			iter != end; ++iter) {
		if (!iter->device_name) {
			wlr_log(WLR_INFO, "found default touch configuration");
			return &*iter;
		}
	}
	return NULL;
}

struct touch_config_entry *
touch_find_config_for_device(const char *device_name)
{
	wlr_log(WLR_INFO, "find touch configuration for %s", device_name);
	for (auto iter = rc.touch_configs.rbegin(),
			end = rc.touch_configs.rend();
			iter != end; ++iter) {
		if (iter->device_name && !strcasecmp(iter->device_name.c(), device_name)) {
			wlr_log(WLR_INFO, "found touch configuration for %s", device_name);
			return &*iter;
		}
	}
	return find_default_config();
}
