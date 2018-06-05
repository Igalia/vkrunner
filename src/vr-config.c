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
               "PPM image\n"
               "  -d            Show the SPIR-V disassembly\n"
               "  -D TOK=REPL   Replace occurences of TOK with REPL in the "
               "scripts\n");
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

static bool
add_token_replacement(struct vr_config *config,
                      const char *arg)
{
        const char *equals = strchr(arg, '=');

        if (equals == NULL || equals == arg) {
                fprintf(stderr,
                        "invalid token replacement “%s”\n",
                        arg);
                return false;
        }

        struct vr_config_token_replacement *tr = vr_calloc(sizeof *tr);

        tr->token = vr_strndup(arg, equals - arg);
        tr->replacement = vr_strdup(equals + 1);

        vr_list_insert(config->token_replacements.prev, &tr->link);

        return true;
}

struct vr_config *
vr_config_new(int argc, char **argv)
{
        struct vr_config *config = vr_calloc(sizeof *config);

        vr_list_init(&config->scripts);
        vr_list_init(&config->token_replacements);

        while (true) {
                int opt = getopt(argc, argv, "-hi:dD:");

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
                case 'd':
                        config->show_disassembly = true;
                        break;
                case 'D':
                        if (!add_token_replacement(config, optarg))
                                goto error;
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

static void
free_scripts(struct vr_config *config)
{
        struct vr_config_script *script, *tmp;

        vr_list_for_each_safe(script, tmp, &config->scripts, link)
                vr_free(script);
}

static void
free_token_replacements(struct vr_config *config)
{
        struct vr_config_token_replacement *tr, *tmp;

        vr_list_for_each_safe(tr, tmp, &config->token_replacements, link) {
                vr_free(tr->token);
                vr_free(tr->replacement);
                vr_free(tr);
        }
}

void
vr_config_free(struct vr_config *config)
{
        free_scripts(config);
        free_token_replacements(config);

        vr_free(config->image_filename);

        vr_free(config);
}
