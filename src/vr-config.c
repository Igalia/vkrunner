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

#include "vr-config.h"

#include <stdio.h>
#include <unistd.h>

static void
show_help(void)
{
        printf("usage: vkrunner [OPTION]... SCRIPT...\n"
               "Runs the shader test script SCRIPT\n"
               "\n"
               "Options:\n"
               "  -h            Show this help message\n"
               "  -i IMG        Write the final rendering to IMG as a "
               "PPM image\n");
}

static void
add_script(struct vr_config *config,
           const char *filename)
{
        struct vr_config_script *script =
                vr_alloc(sizeof *script + strlen(filename) + 1);
        strcpy(script->filename, filename);
        vr_list_insert(config->scripts.prev, &script->link);
}

struct vr_config *
vr_config_new(int argc, char **argv)
{
        struct vr_config *config = vr_calloc(sizeof *config);

        vr_list_init(&config->scripts);

        while (true) {
                int opt = getopt(argc, argv, "-hi:");

                if (opt == -1)
                        break;

                switch (opt) {
                case 'h':
                        show_help();
                        goto error;
                case 'i':
                        if (config->image_filename) {
                                fprintf(stderr,
                                        "duplicate -i option\n");
                                goto error;
                        }
                        config->image_filename = vr_strdup(optarg);
                        break;
                case 1:
                        add_script(config, optarg);
                        break;
                case '?':
                        goto error;
                }
        }

        if (vr_list_empty(&config->scripts)) {
                fprintf(stderr, "no script specified\n");
                show_help();
                goto error;
        }

        return config;

error:
        vr_config_free(config);
        return NULL;
}

void
vr_config_free(struct vr_config *config)
{
        struct vr_config_script *script, *tmp;

        vr_list_for_each_safe(script, tmp, &config->scripts, link)
                vr_free(script);

        vr_free(config->image_filename);

        vr_free(config);
}
