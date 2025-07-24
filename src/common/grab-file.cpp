// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read file into memory
 *
 * Copyright Johan Malm 2020
 */

#define _POSIX_C_SOURCE 200809L
#include "common/grab-file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

lab_str
grab_file(const char *filename)
{
	char *line = NULL;
	size_t len = 0;
	FILE *stream = fopen(filename, "r");
	if (!stream) {
		return lab_str();
	}
	lab_str buffer;
	while ((getline(&line, &len, stream) != -1)) {
		char *p = strrchr(line, '\n');
		if (p) {
			*p = '\0';
		}
		buffer += line;
	}
	free(line);
	fclose(stream);
	return buffer;
}
