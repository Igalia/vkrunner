/*
 * vkrunner
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#define _GNU_SOURCE

#include "vr-strtof.h"

#include <stdlib.h>

#ifdef __GLIBC__
#include <locale.h>
#endif

void
vr_strtof_init(struct vr_strtof_data *data)
{
#ifdef __GLIBC__
        *(locale_t *) data = newlocale(LC_CTYPE_MASK, "C", NULL);
#endif
}

float
vr_strtof(const struct vr_strtof_data *data,
          const char *nptr,
          char **endptr)
{
#ifdef __GLIBC__
        locale_t c_locale = *(locale_t *) data;
        if (c_locale)
                return strtof_l(nptr, endptr, c_locale);
#endif
        return strtof(nptr, endptr);
}

double
vr_strtod(const struct vr_strtof_data *data,
          const char *nptr,
          char **endptr)
{
#ifdef __GLIBC__
        locale_t c_locale = *(locale_t *) data;
        if (c_locale)
                return strtod_l(nptr, endptr, c_locale);
#endif
        return strtod(nptr, endptr);
}

void
vr_strtof_destroy(struct vr_strtof_data *data)
{
#ifdef __GLIBC__
        locale_t c_locale = *(locale_t *) data;
        if (c_locale)
                freelocale(c_locale);
#endif
}
