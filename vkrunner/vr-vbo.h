/*
 * Copyright © 2011, 2016, 2018 Intel Corporation
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

#ifndef VR_VBO_H
#define VR_VBO_H

#include <stdlib.h>
#include <stdbool.h>

#include "vr-list.h"
#include "vr-format.h"
#include "vr-config-private.h"

struct vr_vbo_attrib {
        struct vr_list link;

        const struct vr_format *format;

        /**
         * Vertex location
         */
        unsigned location;

        /**
         * Byte offset into the vertex data of this attribute.
         */
        size_t offset;
};

struct vr_vbo {
        /**
         * Description of each attribute.
         */
        struct vr_list attribs;

        /**
         * Raw data buffer containing parsed numbers.
         */
        uint8_t *raw_data;

        /**
         * Number of bytes in each row of raw_data.
         */
        size_t stride;

        /**
         * Number of rows in raw_data.
         */
        size_t num_rows;
};

struct vr_vbo *
vr_vbo_parse(const struct vr_config *config,
             const char *text,
             size_t text_length);

void
vr_vbo_for_each_attrib(const struct vr_vbo *vbo,
                       void (* func)(const struct vr_vbo_attrib *, void *),
                       void *user_data);

void
vr_vbo_free(struct vr_vbo *vbo);

#endif /* VR_VBO_H */
