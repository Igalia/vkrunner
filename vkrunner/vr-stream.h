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

#ifndef VR_STREAM_H
#define VR_STREAM_H

#include <stdio.h>
#include <stdbool.h>

#include "vr-buffer.h"

enum vr_stream_type {
        VR_STREAM_TYPE_STRING,
        VR_STREAM_TYPE_FILE
};

struct vr_stream {
        enum vr_stream_type type;
        union {
                FILE *file;
                struct {
                        const char *string;
                        const char *end;
                };
        };
};

void
vr_stream_init_string(struct vr_stream *stream,
                      const char *string);

void
vr_stream_init_file(struct vr_stream *stream,
                    FILE *file);

bool
vr_stream_read_line(struct vr_stream *stream,
                    struct vr_buffer *buffer);

#endif /* VR_STREAM_H */
