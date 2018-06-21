/*
 * vkrunner
 *
 * Copyright (C) 2018 Neil Roberts
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

#include <stdio.h>
#include <stdarg.h>

#include "vr-error-message.h"
#include "vr-config-private.h"
#include "vr-buffer.h"

void
vr_error_message(const struct vr_config *config,
                 const char *format, ...)
{
        va_list ap;

        va_start(ap, format);

        if (config->error_cb) {
                struct vr_buffer buf = VR_BUFFER_STATIC_INIT;
                vr_buffer_append_vprintf(&buf, format, ap);
                config->error_cb((const char *) buf.data,
                                 config->user_data);
                vr_buffer_destroy(&buf);
        } else {
                vfprintf(stderr, format, ap);
                fputc('\n', stderr);
        }

        va_end(ap);
}
