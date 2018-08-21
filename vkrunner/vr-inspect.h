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

#ifndef VR_INSPECT_H
#define VR_INSPECT_H

#include <stdlib.h>
#include <vkrunner/vr-format.h>

struct vr_inspect_image {
        /* Dimensions of the buffer */
        int width, height;
        /* The stride in pixels from one row of the image to the next */
        size_t stride;
        /* An opaque pointer describing the format of each pixel in
         * the buffer
         */
        const struct vr_format *format;
        /* The buffer data */
        const void *data;
};

struct vr_inspect_buffer {
        /* The binding number of the buffer */
        int binding;
        /* Size in bytes of the buffer */
        size_t size;
        /* The buffer data */
        const void *data;
};

struct vr_inspect_data {
        /* The color buffer */
        struct vr_inspect_image color_buffer;
        /* An array of buffers used as UBOs or SSBOs */
        size_t n_buffers;
        const struct vr_inspect_buffer *buffers;
};

#endif /* VR_CONFIG_H */
