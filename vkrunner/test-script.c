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

#include <criterion/criterion.h>

#include "vr-script.h"
#include "vr-executor-private.h"

Test(script, line_num) {
        struct vr_executor *executor = vr_executor_new();
        const struct vr_config *config = vr_executor_get_config(executor);
        struct vr_source *source = vr_source_from_string(
                "[test]\n"
                "clear\n"
                "\n"
                "# comment\n"
                "draw rect -1 -1 2 2\n"
                "draw rect -1 \\\n"
                "          -1 \\\n"
                "          2 \\\n"
                "          2\n"
                "draw rect 0 0 1 1");
        struct vr_script *script = vr_script_load(config, source);

        cr_assert(script);

        cr_assert_eq(script->n_commands, 4);
        cr_assert_eq(script->commands[0].line_num, 2);
        cr_assert_eq(script->commands[1].line_num, 5);
        cr_assert_eq(script->commands[2].line_num, 6);
        cr_assert_eq(script->commands[3].line_num, 10);

        vr_script_free(script);
        vr_source_free(source);
        vr_executor_free(executor);
}
