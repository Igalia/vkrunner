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

#include "vr-unit-test.h"
#include "vr-list.h"

#ifdef BUILD_VKRUNNER_TESTS

struct vr_list vr_unit_tests;

struct vr_unit_test {
        struct vr_list link;
        const char *name;
        vr_unit_test_func func;
};

static void
init_tests(void)
{
        static bool initialized = false;

        if (!initialized) {
                vr_list_init(&vr_unit_tests);
                initialized = true;
        }
}

void
vr_unit_test_add(const char *name,
                 vr_unit_test_func func)
{
        init_tests();

        struct vr_unit_test *test = vr_alloc(sizeof *test);

        test->name = name;
        test->func = func;
        vr_list_insert(vr_unit_tests.next, &test->link);
}

void
vr_unit_test_run(const char *name)
{
        init_tests();

        struct vr_unit_test *test;

        vr_list_for_each(test, &vr_unit_tests, link) {
                if (!strcmp(test->name, name)) {
                        test->func();
                        return;
                }
        };

        vr_fatal("No test found named “%s”", name);
}

#endif /* BUILD_VKRUNNER_TESTS */
