/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Very simple C string buffer implementation
 *
 * Copyright Johan Malm 2020
 */

#ifndef LABWC_BUF_H
#define LABWC_BUF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buf {
	char *buf;
	int alloc;
	int len;
};

/**
 * buf_expand_tilde - expand ~ in buffer
 * @s: buffer
 */
void buf_expand_tilde(struct buf *s);

/**
 * buf_expand_shell_variables - expand $foo and ${foo} in buffer
 * @s: buffer
 * Note: $$ is not handled
 */
void buf_expand_shell_variables(struct buf *s);

/**
 * buf_init - allocate NULL-terminated C string buffer
 * @s: buffer
 * Note: use free(s->buf) to free it
 */
void buf_init(struct buf *s);

/**
 * buf_add - add data to C string buffer
 * @s: buffer
 * @data: data to be added
 */
void buf_add(struct buf *s, const char *data);

/**
 * buf_clear - clear the buffer, internal allocations are preserved
 * @s: buffer
 */
void buf_clear(struct buf *s);

#endif /* LABWC_BUF_H */
