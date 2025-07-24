/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Very simple C string buffer implementation
 *
 * Copyright Johan Malm 2020
 */

#ifndef LABWC_BUF_H
#define LABWC_BUF_H

#include "common/str.h"

/**
 * buf_expand_tilde - expand ~ in buffer
 * @s: buffer
 */
lab_str buf_expand_tilde(const char *s);

/**
 * buf_expand_shell_variables - expand $foo and ${foo} in buffer
 * @s: buffer
 * Note: $$ is not handled
 */
lab_str buf_expand_shell_variables(const char *s);

#endif /* LABWC_BUF_H */
