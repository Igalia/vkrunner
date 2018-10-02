/*
 * Copyright (c) Intel Corporation 2018
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEM, IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VR_HEX_H
#define VR_HEX_H

#include <stdint.h>
#include "vr-strtof.h"

float
vr_hex_strtof(const struct vr_strtof_data *data,
              const char *nptr,
              char **endptr);

double
vr_hex_strtod(const struct vr_strtof_data *data,
              const char *nptr,
              char **endptr);

int
vr_hex_strtol(const struct vr_strtof_data *data,
              const char *nptr,
              char **endptr);

uint16_t
vr_hex_strtohf(const struct vr_strtof_data *data,
               const char *nptr,
               char **endptr);

#endif /* VR_HEX_H */
