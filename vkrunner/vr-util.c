/*
 * vkrunner
 *
 * Copyright (C) 2013, 2014 Neil Roberts
 * Copyright © 2015 Intel Corporation
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

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vr-util.h"

void
vr_fatal(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);

        fputc('\n', stderr);

        fflush(stderr);

        abort();
}

void *
vr_alloc(size_t size)
{
        void *result = malloc(size);

        if (result == NULL)
                vr_fatal("Memory exhausted");

        return result;
}

void *
vr_calloc(size_t size)
{
        void *result = vr_alloc(size);

        memset(result, 0, size);

        return result;
}

void *
vr_realloc(void *ptr, size_t size)
{
        if (ptr == NULL)
                return vr_alloc(size);

        ptr = realloc(ptr, size);

        if (ptr == NULL)
                vr_fatal("Memory exhausted");

        return ptr;
}

char *
vr_strdup(const char *str)
{
        return vr_memdup(str, strlen(str) + 1);
}

char *
vr_strndup(const char *str, size_t size)
{
        const char *end = str;

        while (end - str < size && *end != '\0')
                end++;

        size = end - str;

        char *ret = vr_alloc(size + 1);
        memcpy(ret, str, size);
        ret[size] = '\0';

        return ret;
}

void *
vr_memdup(const void *data, size_t size)
{
        void *ret;

        ret = vr_alloc(size);
        memcpy(ret, data, size);

        return ret;
}

char *
vr_strconcat(const char *string1, ...)
{
        size_t string1_length;
        size_t total_length;
        size_t str_length;
        va_list ap, apcopy;
        const char *str;
        char *result, *p;

        if (string1 == NULL)
                return vr_strdup("");

        total_length = string1_length = strlen(string1);

        va_start(ap, string1);
        va_copy(apcopy, ap);

        while ((str = va_arg(ap, const char *)))
                total_length += strlen(str);

        va_end(ap);

        result = vr_alloc(total_length + 1);
        memcpy(result, string1, string1_length);
        p = result + string1_length;

        while ((str = va_arg(apcopy, const char *))) {
                str_length = strlen(str);
                memcpy(p, str, str_length);
                p += str_length;
        }
        *p = '\0';

        va_end(apcopy);

        return result;
}

void
vr_free(void *ptr)
{
        if (ptr)
                free(ptr);
}

/**
 * Reads an environment variable and interprets its value as a boolean.
 *
 * Recognizes 0/false/no and 1/true/yes. Other values result in the
 * default value.
 */
bool
vr_env_var_as_boolean(const char *var_name, bool default_value)
{
        const char *str = getenv(var_name);
        if (str == NULL)
                return default_value;

        if (strcmp(str, "1") == 0 ||
            strcasecmp(str, "true") == 0 ||
            strcasecmp(str, "yes") == 0) {
                return true;
        } else if (strcmp(str, "0") == 0 ||
                   strcasecmp(str, "false") == 0 ||
                   strcasecmp(str, "no") == 0) {
                return false;
        } else {
                return default_value;
        }
}

#ifndef HAVE_FFS

int
vr_util_ffs(int value)
{
        int pos = 1;

        if (value == 0)
                return 0;

        while ((value & 1) == 0) {
                value >>= 1;
                pos++;
        }

        return pos;
}

#endif

#ifndef HAVE_FFSL

int
vr_util_ffsl(long int value)
{
        int pos = vr_util_ffs(value);

        if (pos)
                return pos;

        pos = vr_util_ffs(value >> ((sizeof (long int) - sizeof (int)) * 8));

        if (pos)
                return pos + (sizeof (long int) - sizeof (int)) * 8;

        return 0;
}

#endif
