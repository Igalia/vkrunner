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

#ifndef VR_CONFIG_PRIVATE_H
#define VR_CONFIG_PRIVATE_H

#include "vr-config.h"
#include "vr-list.h"

struct vr_config_script {
        struct vr_list link;
        char *filename;
        char *string;
};

struct vr_config_token_replacement {
        struct vr_list link;
        char *token;
        char *replacement;
};

struct vr_config {
        char *image_filename;
        struct vr_list scripts;
        struct vr_list token_replacements;
        bool show_disassembly;

        vr_config_error_cb error_cb;
        vr_config_before_test_cb before_test_cb;
        vr_config_after_test_cb after_test_cb;
        void *user_data;

        /* Names of instance layers and extensions */
        const char *const *layers;
        const char *const *extensions;
};

#endif /* VR_CONFIG_PRIVATE_H */
