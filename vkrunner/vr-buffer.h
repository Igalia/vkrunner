/*
 * vkrunner
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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

#ifndef VR_BUFFER_H
#define VR_BUFFER_H

#include <stdint.h>
#include <stdarg.h>

#include "vr-util.h"

struct vr_buffer {
        uint8_t *data;
        size_t length;
        size_t size;
};

#define VR_BUFFER_STATIC_INIT { .data = NULL, .length = 0, .size = 0 }

void
vr_buffer_init(struct vr_buffer *buffer);

void
vr_buffer_ensure_size(struct vr_buffer *buffer,
                      size_t size);

void
vr_buffer_set_length(struct vr_buffer *buffer,
                     size_t length);

VR_PRINTF_FORMAT(2, 3) void
vr_buffer_append_printf(struct vr_buffer *buffer,
                        const char *format, ...);

void
vr_buffer_append_vprintf(struct vr_buffer *buffer,
                         const char *format,
                         va_list ap);

void
vr_buffer_append(struct vr_buffer *buffer,
                 const void *data,
                 size_t length);

static VR_INLINE void
vr_buffer_append_c(struct vr_buffer *buffer,
                   char c)
{
        if (buffer->size > buffer->length)
                buffer->data[buffer->length++] = c;
        else
                vr_buffer_append(buffer, &c, 1);
}

void
vr_buffer_append_string(struct vr_buffer *buffer,
                        const char *str);

void
vr_buffer_destroy(struct vr_buffer *buffer);

#endif /* VR_BUFFER_H */
