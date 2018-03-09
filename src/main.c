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

#include <stdlib.h>
#include <stdbool.h>

#include "vr-vk.h"
#include "vr-window.h"
#include "vr-script.h"
#include "vr-pipeline.h"

static bool
process_script(struct vr_window *window,
               const char *filename)
{
        bool ret = true;
        struct vr_script *script = vr_script_load(filename);

        if (script == NULL)
                return false;

        struct vr_pipeline *pipeline = vr_pipeline_create(window, script);

        if (pipeline == NULL) {
                ret = false;
        } else {
                vr_pipeline_free(pipeline);
        }

        vr_script_free(script);

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;
        struct vr_window *window = NULL;
        int i;

        window = vr_window_new();
        if (window == NULL) {
                ret = EXIT_FAILURE;
                goto out;
        }

        for (i = 1; i < argc; i++) {
                if (!process_script(window, argv[i])) {
                        ret = EXIT_FAILURE;
                        goto out;
                }
        }

out:
        if (window)
                vr_window_free(window);

        return ret;
}
