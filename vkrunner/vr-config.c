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

#include "vr-config-private.h"

#include <string.h>

void
vr_config_add_script_file(struct vr_config *config,
                          const char *filename)
{
        struct vr_config_script *script;
        script = vr_alloc(sizeof *script);
        script->filename = vr_strdup(filename);
        script->string = NULL;
        vr_list_insert(config->scripts.prev, &script->link);
}

void
vr_config_add_script_string(struct vr_config *config,
                            const char *string)
{
        struct vr_config_script *script;
        script = vr_alloc(sizeof *script);
        script->filename = NULL;
        script->string = vr_strdup(string);
        vr_list_insert(config->scripts.prev, &script->link);
}

void
vr_config_add_token_replacement(struct vr_config *config,
                                const char *token,
                                const char *replacement)
{
        struct vr_config_token_replacement *tr = vr_calloc(sizeof *tr);

        tr->token = vr_strdup(token);
        tr->replacement = vr_strdup(replacement);

        vr_list_insert(config->token_replacements.prev, &tr->link);
}

void
vr_config_set_image_filename(struct vr_config *config,
                             const char *image_filename)
{
        vr_free(config->image_filename);
        config->image_filename = vr_strdup(image_filename);
}

void
vr_config_set_show_disassembly(struct vr_config *config,
                               bool show_disassembly)
{
        config->show_disassembly = show_disassembly;
}

void
vr_config_set_user_data(struct vr_config *config,
                        void *user_data)
{
        config->user_data = user_data;
}

void
vr_config_set_error_cb(struct vr_config *config,
                       vr_config_error_cb error_cb)
{
        config->error_cb = error_cb;
}

void
vr_config_set_before_test_cb(struct vr_config *config,
                             vr_config_before_test_cb before_test_cb)
{
        config->before_test_cb = before_test_cb;
}

void
vr_config_set_after_test_cb(struct vr_config *config,
                            vr_config_after_test_cb after_test_cb)
{
        config->after_test_cb = after_test_cb;
}

struct vr_config *
vr_config_new(void)
{
        struct vr_config *config = vr_calloc(sizeof *config);

        vr_list_init(&config->scripts);
        vr_list_init(&config->token_replacements);

        return config;
}

static void
free_scripts(struct vr_config *config)
{
        struct vr_config_script *script, *tmp;

        vr_list_for_each_safe(script, tmp, &config->scripts, link) {
                vr_free(script->filename);
                vr_free(script->string);
                vr_free(script);
        }
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
