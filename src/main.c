// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "common/dir.h"
#include "common/fd_util.h"
#include "common/font.h"
#include "common/mem.h"
#include "common/spawn.h"
#include "config/session.h"
#include "labwc.h"
#include "theme.h"
#include "menu/menu.h"

#include <dlfcn.h>

static void *(*real_malloc)(size_t);
static void *(*real_calloc)(size_t, size_t);
static void *(*real_realloc)(void *, size_t);

static uint64_t num_allocs;
static uint64_t max_allocs;

static bool out_of_memory()
{
	if (!max_allocs) {
		srand(time(NULL));
		max_allocs = rand() % 200000;
	}
	if (++num_allocs >= max_allocs) {
		errno = ENOMEM;
		return true;
	}
	return false;
}

void *malloc(size_t size)
{
	if (!real_malloc) {
		real_malloc = dlsym(RTLD_NEXT, "malloc");
	}
	return out_of_memory() ? NULL :
		real_malloc(size);
}

void *calloc(size_t nmemb, size_t size)
{
	if (!real_calloc) {
		real_calloc = dlsym(RTLD_NEXT, "calloc");
	}
	return out_of_memory() ? NULL :
		real_calloc(nmemb, size);
}

void *realloc(void *ptr, size_t size)
{
	if (!real_realloc) {
		real_realloc = dlsym(RTLD_NEXT, "realloc");
	}
	return out_of_memory() ? NULL :
		real_realloc(ptr, size);
}

struct rcxml rc = { 0 };

static const char labwc_usage[] =
"Usage: labwc [options...]\n"
"    -c <config-file>    specify config file (with path)\n"
"    -C <config-dir>     specify config directory\n"
"    -d                  enable full logging, including debug information\n"
"    -h                  show help message and quit\n"
"    -s <command>        run command on startup\n"
"    -v                  show version number and quit\n"
"    -V                  enable more verbose logging\n";

static void
usage(void)
{
	printf("%s", labwc_usage);
	exit(0);
}

int
main(int argc, char *argv[])
{
#if HAVE_NLS
	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
	textdomain(GETTEXT_PACKAGE);
#endif
	char *startup_cmd = NULL;
	char *config_file = NULL;
	enum wlr_log_importance verbosity = WLR_ERROR;

	int c;
	while ((c = getopt(argc, argv, "c:C:dhs:vV")) != -1) {
		switch (c) {
		case 'c':
			config_file = optarg;
			break;
		case 'C':
			rc.config_dir = xstrdup(optarg);
			break;
		case 'd':
			verbosity = WLR_DEBUG;
			break;
		case 's':
			startup_cmd = optarg;
			break;
		case 'v':
			printf("labwc " LABWC_VERSION "\n");
			exit(0);
		case 'V':
			verbosity = WLR_INFO;
			break;
		case 'h':
		default:
			usage();
		}
	}
	if (optind < argc) {
		usage();
	}

	wlr_log_init(verbosity, NULL);

	if (!rc.config_dir) {
		rc.config_dir = config_dir();
	}
	wlr_log(WLR_INFO, "using config dir (%s)\n", rc.config_dir);
	session_environment_init(rc.config_dir);
	rcxml_read(config_file);

	if (!getenv("XDG_RUNTIME_DIR")) {
		wlr_log(WLR_ERROR, "XDG_RUNTIME_DIR is unset");
		exit(EXIT_FAILURE);
	}

	increase_nofile_limit();

	struct server server = { 0 };
	server_init(&server);
	server_start(&server);

	struct theme theme = { 0 };
	theme_init(&theme, rc.theme_name);
	rc.theme = &theme;
	server.theme = &theme;

	menu_init_rootmenu(&server);
	menu_init_windowmenu(&server);

	session_autostart_init(rc.config_dir);
	if (startup_cmd) {
		spawn_async_no_shell(startup_cmd);
	}

	wl_display_run(server.wl_display);

	server_finish(&server);

	menu_finish();
	theme_finish(&theme);
	rcxml_finish();
	font_finish();
	return 0;
}
