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

#include "vr-execute.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "vr-vk.h"
#include "vr-window.h"
#include "vr-script.h"
#include "vr-pipeline.h"
#include "vr-test.h"
#include "vr-config-private.h"
#include "vr-error-message.h"

static bool
write_ppm(struct vr_window *window,
          const char *filename)
{
        const struct vr_format *format = window->framebuffer_format;
        int format_size = vr_format_get_size(format);
        FILE *out = fopen(filename, "w");

        if (out == NULL) {
                vr_error_message(window->config, "%s: %s", filename, strerror(errno));
                return false;
        }

        fprintf(out,
                "P6\n"
                "%i %i\n"
                "255\n",
                VR_WINDOW_WIDTH,
                VR_WINDOW_HEIGHT);

        for (int y = 0; y < VR_WINDOW_HEIGHT; y++) {
                const uint8_t *p = ((uint8_t *) window->linear_memory_map +
                                    y * window->linear_memory_stride);

                for (int x = 0; x < VR_WINDOW_WIDTH; x++) {
                        double pixel[4];

                        vr_format_load_pixel(format, p, pixel);

                        for (int i = 0; i < 3; i++) {
                                double v = pixel[i];

                                if (v < 0.0)
                                        v = 0.0;
                                else if (v > 1.0)
                                        v = 1.0;

                                fputc(round(v * 255.0), out);
                        }
                        p += format_size;
                }
        }

        fclose(out);

        return true;
}

static enum vr_result
process_script(const struct vr_config *config,
               const char *filename)
{
        enum vr_result res = VR_RESULT_PASS;
        struct vr_script *script = NULL;
        struct vr_window *window = NULL;
        struct vr_pipeline *pipeline = NULL;

        script = vr_script_load(config, filename);
        if (script == NULL) {
                res = VR_RESULT_FAIL;
                goto out;
        }

        res = vr_window_new(config,
                            &script->required_features,
                            script->extensions,
                            script->framebuffer_format,
                            script->depth_stencil_format,
                            &window);
        if (res != VR_RESULT_PASS)
                goto out;

        pipeline = vr_pipeline_create(config, window, script);

        if (pipeline == NULL) {
                res = VR_RESULT_FAIL;
                goto out;
        }

        if (!vr_test_run(window, pipeline, script))
                res = VR_RESULT_FAIL;

        if (config->image_filename) {
                if (!write_ppm(window, config->image_filename))
                        res = VR_RESULT_FAIL;
        }

out:
        if (pipeline)
                vr_pipeline_free(pipeline);
        if (window)
                vr_window_free(window);
        if (script)
                vr_script_free(script);

        return res;
}

enum vr_result
vr_execute(const struct vr_config *config)
{
        enum vr_result overall_result = VR_RESULT_SKIP;

        const struct vr_config_script *script;
        vr_list_for_each(script, &config->scripts, link) {
                if (config->scripts.next->next != &config->scripts)
                        printf("%s\n", script->filename);

                enum vr_result res = process_script(config, script->filename);
                overall_result = vr_result_merge(res, overall_result);
        }

        return overall_result;
}
