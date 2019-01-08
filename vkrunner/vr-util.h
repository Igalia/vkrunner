/*
 * vkrunner
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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

#ifndef VR_UTIL_H
#define VR_UTIL_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Include the right header for alloca */
#ifdef WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif

#ifdef _MSC_VER
#define VR_INLINE __inline
#else
#define VR_INLINE inline
#endif

#define VR_SWAP_UINT16(x)                       \
  ((uint16_t)                                   \
   (((uint16_t) (x) >> 8) |                     \
    ((uint16_t) (x) << 8)))
#define VR_SWAP_UINT32(x)                               \
  ((uint32_t)                                           \
   ((((uint32_t) (x) & UINT32_C (0x000000ff)) << 24) |  \
    (((uint32_t) (x) & UINT32_C (0x0000ff00)) << 8) |   \
    (((uint32_t) (x) & UINT32_C (0x00ff0000)) >> 8) |   \
    (((uint32_t) (x) & UINT32_C (0xff000000)) >> 24)))
#define VR_SWAP_UINT64(x)                                               \
  ((uint64_t)                                                           \
   ((((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000000000ff)) << 56) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000000000ff00)) << 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000000000ff0000)) << 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000ff000000)) << 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000ff00000000)) >> 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000ff0000000000)) >> 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00ff000000000000)) >> 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0xff00000000000000)) >> 56)))

#if defined(HAVE_BIG_ENDIAN)
#define VR_UINT16_FROM_BE(x) (x)
#define VR_UINT32_FROM_BE(x) (x)
#define VR_UINT64_FROM_BE(x) (x)
#define VR_UINT16_FROM_LE(x) VR_SWAP_UINT16 (x)
#define VR_UINT32_FROM_LE(x) VR_SWAP_UINT32 (x)
#define VR_UINT64_FROM_LE(x) VR_SWAP_UINT64 (x)
#elif defined(HAVE_LITTLE_ENDIAN)
#define VR_UINT16_FROM_LE(x) (x)
#define VR_UINT32_FROM_LE(x) (x)
#define VR_UINT64_FROM_LE(x) (x)
#define VR_UINT16_FROM_BE(x) VR_SWAP_UINT16 (x)
#define VR_UINT32_FROM_BE(x) VR_SWAP_UINT32 (x)
#define VR_UINT64_FROM_BE(x) VR_SWAP_UINT64 (x)
#else
#error Platform is neither little-endian nor big-endian
#endif

#define VR_UINT16_TO_LE(x) VR_UINT16_FROM_LE (x)
#define VR_UINT16_TO_BE(x) VR_UINT16_FROM_BE (x)
#define VR_UINT32_TO_LE(x) VR_UINT32_FROM_LE (x)
#define VR_UINT32_TO_BE(x) VR_UINT32_FROM_BE (x)
#define VR_UINT64_TO_LE(x) VR_UINT64_FROM_LE (x)
#define VR_UINT64_TO_BE(x) VR_UINT64_FROM_BE (x)

#define VR_STMT_START do
#define VR_STMT_END while (0)

#define VR_N_ELEMENTS(array)                    \
  (sizeof (array) / sizeof ((array)[0]))

#define VR_STRINGIFY(macro_or_string) VR_STRINGIFY_ARG (macro_or_string)
#define VR_STRINGIFY_ARG(contents) #contents

#ifdef __GNUC__
#define VR_NO_RETURN __attribute__((noreturn))
#define VR_PRINTF_FORMAT(string_index, first_to_check) \
  __attribute__((format(printf, string_index, first_to_check)))
#define VR_NULL_TERMINATED __attribute__((sentinel))
#else
#define VR_PRINTF_FORMAT(string_index, first_to_check)
#define VR_NULL_TERMINATED
#ifdef _MSC_VER
#define VR_NO_RETURN __declspec(noreturn)
#else
#define VR_NO_RETURN
#endif
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define VR_STMT_START do
#define VR_STMT_END while (0)

void *
vr_alloc(size_t size);

void *
vr_calloc(size_t size);

void *
vr_realloc(void *ptr, size_t size);

char *
vr_strdup(const char *str);

char *
vr_strndup(const char *str, size_t size);

void *
vr_memdup(const void *data, size_t size);

VR_NULL_TERMINATED char *
vr_strconcat(const char *string1, ...);

void
vr_free(void *ptr);

bool
vr_env_var_as_boolean(const char *var_name, bool default_value);

VR_NO_RETURN
VR_PRINTF_FORMAT(1, 2)
void
vr_fatal(const char *format, ...);

#ifdef HAVE_FFS
#include <strings.h>
#define vr_util_ffs(x) ffs(x)
#else
int
vr_util_ffs(int value);
#endif

#ifdef HAVE_FFSL
#include <string.h>
#define vr_util_ffsl(x) ffsl(x)
#else
int
vr_util_ffsl(long int value);
#endif

#ifdef WIN32
#define VR_PATH_SEPARATOR "\\"
#else
#define VR_PATH_SEPARATOR "/"
#endif

/**
 * Align a value, only works pot alignemnts.
 */
static VR_INLINE int
vr_align(int value, int alignment)
{
   return (value + alignment - 1) & ~(alignment - 1);
}

#ifdef WIN32
#define vr_strcasecmp _stricmp
#else
#define vr_strcasecmp strcasecmp
#endif

#endif /* VR_UTIL_H */
