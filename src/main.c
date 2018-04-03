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
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include "vr-vk.h"
#include "vr-window.h"
#include "vr-script.h"
#include "vr-pipeline.h"
#include "vr-test.h"
#include "vr-config.h"
#include "vr-error-message.h"

static bool
write_ppm(struct vr_window *window,
          const char *filename)
{
        const struct vr_format *format = window->framebuffer_format;
        int format_size = vr_format_get_size(format);
        FILE *out = fopen(filename, "w");

        if (out == NULL) {
                vr_error_message("%s: %s", filename, strerror(errno));
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

static bool
process_script(struct vr_config *config,
               const char *filename)
{
        bool ret = true;
        struct vr_script *script = NULL;
        struct vr_window *window = NULL;
        struct vr_pipeline *pipeline = NULL;

        script = vr_script_load(filename);
        if (script == NULL) {
                ret = false;
                goto out;
        }

        window = vr_window_new(&script->required_features,
                               script->framebuffer_format);
        if (window == NULL) {
                ret = false;
                goto out;
        }

        pipeline = vr_pipeline_create(config, window, script);

        if (pipeline == NULL) {
                ret = false;
                goto out;
        }

        if (!vr_test_run(window, pipeline, script))
                ret = false;

        if (config->image_filename) {
                if (!write_ppm(window, config->image_filename))
                        ret = false;
        }

out:
        if (pipeline)
                vr_pipeline_free(pipeline);
        if (window)
                vr_window_free(window);
        if (script)
                vr_script_free(script);

        return ret;
}

int
main(int argc, char **argv)
{
        int ret = EXIT_SUCCESS;

        struct vr_config *config = vr_config_new(argc, argv);
        if (config == NULL)
                return EXIT_FAILURE;

        struct vr_config_script *script;
        vr_list_for_each(script, &config->scripts, link) {
                if (config->scripts.next->next != &config->scripts)
                        printf("%s\n", script->filename);
                if (!process_script(config, script->filename)) {
                        ret = EXIT_FAILURE;
                        break;
                }
        }

        vr_config_free(config);

        return ret;
}
