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

#ifndef VR_UNIT_TEST_H
#define VR_UNIT_TEST_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "vr-list.h"
#include "vr-util.h"

typedef void (* vr_unit_test_func)(void);

void
vr_unit_test_add(const char *name,
                 vr_unit_test_func func);

void
vr_unit_test_run(const char *name);

#ifdef BUILD_VKRUNNER_TESTS

#define VR_UNIT_TEST(name, code)                                        \
        static void                                                     \
        unit_test_ ## name ## _callback(void)                           \
        {                                                               \
                do { code } while (0);                                  \
        }                                                               \
                                                                        \
        static void __attribute__((constructor))                        \
        unit_test_ ## name ## _initializer(void)                        \
        {                                                               \
                vr_unit_test_add(VR_STRINGIFY(name),                    \
                                 unit_test_ ## name ## _callback);      \
        }

#define VR_UNIT_TEST_ASSERT(expr)                               \
        do {                                                    \
                if (!(expr)) {                                  \
                        fprintf(stderr,                         \
                                "%s:%i: Assertion failed\n",    \
                                __FILE__,                       \
                                __LINE__);                      \
                        abort();                                \
                }                                               \
        } while (0)

#else /* BUILD_VKRUNNER_TESTS */

#define VR_UNIT_TEST_ASSERT(expr)
#define VR_UNIT_TEST(name, code)

#endif /* BUILD_VKRUNNER_TESTS */

#endif /* VR_UNIT_TEST_H */
