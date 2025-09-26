// SPDX-License-Identifier: GPL-2.0-only
/*
 * Find the configuration and theme directories
 *
 * Copyright Johan Malm 2020
 */
#include "common/dir.h"
#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/buf.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"

struct dir {
	const char *prefix;
	const char *default_prefix;
	const char *path;
};

static struct dir config_dirs[] = {
	{
		.prefix = "XDG_CONFIG_HOME",
		.default_prefix = "$HOME/.config",
		.path = "labwc"
	}, {
		.prefix = "XDG_CONFIG_DIRS",
		.default_prefix = "/etc/xdg",
		.path = "labwc",
	}, {
		.path = NULL,
	}
};

static struct dir theme_dirs[] = {
	{
		.prefix = "XDG_DATA_HOME",
		.default_prefix = "$HOME/.local/share",
		.path = "themes",
	}, {
		.prefix = "HOME",
		.path = ".themes",
	}, {
		.prefix = "XDG_DATA_DIRS",
		.default_prefix = "/usr/share:/usr/local/share:/opt/share",
		.path = "themes",
	}, {
		.path = NULL,
	}
};

struct ctx {
	void (*build_path_fn)(struct ctx *ctx, char *prefix, const char *path);
	const char *filename;
	char *buf;
	size_t len;
	struct dir *dirs;
	const char *theme_name;
	std::vector<lab_str> &list;
};

struct wl_list *paths_get_prev(struct wl_list *elm) { return elm->prev; }
struct wl_list *paths_get_next(struct wl_list *elm) { return elm->next; }

static void
build_config_path(struct ctx *ctx, char *prefix, const char *path)
{
	assert(prefix);
	snprintf(ctx->buf, ctx->len, "%s/%s/%s", prefix, path, ctx->filename);
}

static void
build_theme_path_labwc(struct ctx *ctx, char *prefix, const char *path)
{
	assert(prefix);
	snprintf(ctx->buf, ctx->len, "%s/%s/%s/labwc/%s", prefix, path,
		ctx->theme_name, ctx->filename);
}

static void
build_theme_path_openbox(struct ctx *ctx, char *prefix, const char *path)
{
	assert(prefix);
	snprintf(ctx->buf, ctx->len, "%s/%s/%s/openbox-3/%s", prefix, path,
		ctx->theme_name, ctx->filename);
}

static void
find_dir(struct ctx *ctx)
{
	char *debug = getenv("LABWC_DEBUG_DIR_CONFIG_AND_THEME");

	struct buf prefix = BUF_INIT;
	for (int i = 0; ctx->dirs[i].path; i++) {
		struct dir d = ctx->dirs[i];
		buf_clear(&prefix);

		/*
		 * Replace (rather than augment) $HOME/.config with
		 * $XDG_CONFIG_HOME if defined, and so on for the other
		 * XDG Base Directories.
		 */
		char *pfxenv = getenv(d.prefix);
		buf_add(&prefix, pfxenv ? pfxenv : d.default_prefix);
		if (!prefix.len) {
			continue;
		}

		/* Handle .default_prefix shell variables such as $HOME */
		buf_expand_shell_variables(&prefix);

		/*
		 * Respect that $XDG_DATA_DIRS can contain multiple colon
		 * separated paths and that we have structured the
		 * .default_prefix in the same way.
		 */
		gchar * *prefixes;
		prefixes = g_strsplit(prefix.data, ":", -1);
		for (gchar * *p = prefixes; *p; p++) {
			ctx->build_path_fn(ctx, *p, d.path);
			if (debug) {
				fprintf(stderr, "%s\n", ctx->buf);
			}

			/*
			 * TODO: We could stat() and continue here if we really
			 * wanted to only respect only the first hit, but feels
			 * like it is probably overkill.
			 */
			ctx->list.push_back(lab_str(ctx->buf));
		}
		g_strfreev(prefixes);
	}
	buf_reset(&prefix);
}

std::vector<lab_str>
paths_config_create(const char *filename)
{
	char buf[4096] = { 0 };
	std::vector<lab_str> paths;

	/*
	 * If user provided a config directory with the -C command line option,
	 * then that trumps everything else and we do not create the
	 * XDG-Base-Dir list.
	 */
	if (rc.config_dir) {
		paths.push_back(strdup_printf("%s/%s", rc.config_dir,
			filename));
		return paths;
	}

	struct ctx ctx = {
		.build_path_fn = build_config_path,
		.filename = filename,
		.buf = buf,
		.len = sizeof(buf),
		.dirs = config_dirs,
		.list = paths,
	};
	find_dir(&ctx);
	return paths;
}

std::vector<lab_str>
paths_theme_create(const char *theme_name, const char *filename)
{
	static char buf[4096] = { 0 };
	std::vector<lab_str> paths;
	struct ctx ctx = {
		.build_path_fn = build_theme_path_labwc,
		.filename = filename,
		.buf = buf,
		.len = sizeof(buf),
		.dirs = theme_dirs,
		.theme_name = theme_name,
		.list = paths,
	};
	find_dir(&ctx);

	ctx.build_path_fn = build_theme_path_openbox;
	find_dir(&ctx);
	return paths;
}
