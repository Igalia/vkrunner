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
#include "vr-util.h"

struct vr_config *
vr_config_new(void)
{
        struct vr_config *config = vr_calloc(sizeof(struct vr_config));
        vr_strtof_init(&config->strtof_data);
        return config;
}


void
vr_config_free(struct vr_config *config)
{
        vr_strtof_destroy(&config->strtof_data);
        vr_free(config);
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
                       vr_callback_error error_cb)
{
        config->error_cb = error_cb;
}

void
vr_config_set_inspect_cb(struct vr_config *config,
                         vr_callback_inspect inspect_cb)
{
        config->inspect_cb = inspect_cb;
}
