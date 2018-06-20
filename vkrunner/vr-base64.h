/*
 * vkrunner
 *
 * Copyright (C) 2014 Neil Roberts
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

#ifndef VR_BASE64_H
#define VR_BASE64_H

#include <stdlib.h>
#include <stdint.h>

#include "vr-buffer.h"

struct vr_base64_data {
        int n_padding;
        int n_chars;
        int value;
};

void
vr_base64_decode_start(struct vr_base64_data *data);

bool
vr_base64_decode(struct vr_base64_data *data,
                 const uint8_t *in_buffer,
                 size_t length,
                 struct vr_buffer *out_buffer);

bool
vr_base64_decode_end(struct vr_base64_data *data,
                     struct vr_buffer *out_buffer);

#endif /* VR_BASE64_H */
