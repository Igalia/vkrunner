/*
 * Copyright (c) The Piglit project 2007
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

#include "config.h"

#include "vr-hex.h"
#include "vr-half-float.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Based on functions from piglit-util.c */

/**
 * Wrapper for strtod() which allows using an exact hex bit
 * pattern to generate a float value.
 */
float
vr_hex_strtof(const char *nptr, char **endptr)
{
        /* skip spaces and tabs */
        while (*nptr == ' ' || *nptr == '\t')
                nptr++;

        if (strncmp(nptr, "0x", 2) == 0) {
                union {
                        uint32_t u;
                        float f;
                } x;

                x.u = strtoul(nptr, endptr, 16);
                return x.f;
        } else {
                return strtod(nptr, endptr);
        }
}

/**
 * Wrapper for strtod() which allows using an exact hex bit
 * pattern to generate a double value.
 */
double
vr_hex_strtod(const char *nptr, char **endptr)
{
        /* skip spaces and tabs */
        while (*nptr == ' ' || *nptr == '\t')
                nptr++;

        if (strncmp(nptr, "0x", 2) == 0) {
                union {
                        uint64_t u64;
                        double d;
                } x;

                x.u64 = strtoull(nptr, endptr, 16);
                return x.d;
        } else {
                return strtod(nptr, endptr);
        }
}

/**
 * Wrapper for strtol() which allows using an exact hex bit pattern to
 * generate a signed int value.
 */
int
vr_hex_strtol(const char *nptr, char **endptr)
{
        /* skip spaces and tabs */
        while (*nptr == ' ' || *nptr == '\t')
                nptr++;

        if (strncmp(nptr, "0x", 2) == 0) {
                union {
                        uint32_t u;
                        int32_t i;
                } x;

                x.u = strtoul(nptr, endptr, 16);
                return x.i;
        } else {
                return strtol(nptr, endptr, 0);
        }
}

/**
 * Wrapper for vr_half_float_from_float() which allows using an exact
 * hex bit pattern to generate a half float value.
 */
uint16_t
vr_hex_strtohf(const char *nptr, char **endptr)
{
        /* skip spaces and tabs */
        while (*nptr == ' ' || *nptr == '\t')
                nptr++;

        if (strncmp(nptr, "0x", 2) == 0) {
                uint32_t u = strtoul(nptr, endptr, 16);
                if (u > UINT16_MAX) {
                        errno = ERANGE;
                        return UINT16_MAX;
                } else {
                        return u;
                }
        } else {
                return vr_half_float_from_float(strtod(nptr, endptr));
        }
}
