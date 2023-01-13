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

#ifndef VR_SOURCE_PRIVATE_H
#define VR_SOURCE_PRIVATE_H

#include "vr-source.h"
#include "vr-list.h"

enum vr_source_type {
        VR_SOURCE_TYPE_FILE,
        VR_SOURCE_TYPE_STRING
};

struct vr_source_token_replacement {
        struct vr_list link;
        char *token;
        char *replacement;
};

struct vr_source {
        enum vr_source_type type;
        struct vr_list token_replacements;
        char string[];
};

/* Returns the filename. If the source is a string then the filename
 * will be "(string source)". The caller is responsible for freeing
 * the string. This is mostly to make it easier to add a terminating
 * null byte during the transition to Rust.
 */
char *
vr_source_get_filename(const struct vr_source *source);

#endif /* VR_SOURCE_PRIVATE_H */
