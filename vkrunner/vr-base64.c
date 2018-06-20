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

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "vr-base64.h"

static int
alphabet_value(int ch)
{
        if (ch == '/') {
                return 63;
        } else if (ch == '+') {
                return 62;
        } else if (ch <= '9') {
                if (ch < '0')
                        return -1;
                return ch - '0' + 26 * 2;
        } else if (ch <= 'Z') {
                if (ch < 'A')
                        return -1;
                return ch - 'A';
        } else if (ch <= 'z') {
                if (ch < 'a')
                        return -1;
                return ch - 'a' + 26;
        } else {
                return -1;
        }
}

void
vr_base64_decode_start(struct vr_base64_data *data)
{
        memset(data, 0, sizeof *data);
}

static bool
handle_padding(struct vr_base64_data *data,
               const uint8_t *in_buffer,
               size_t length)
{
        const uint8_t *in;

        for (in = in_buffer; in - in_buffer < length; in++) {
                if (*in == '=') {
                        if (++data->n_padding > 2) {
                                return false;
                        }
                } else if (alphabet_value(*in) != -1) {
                        return false;
                }
        }

        return true;
}

bool
vr_base64_decode(struct vr_base64_data *data,
                 const uint8_t *in_buffer,
                 size_t length,
                 struct vr_buffer *out_buffer)
{
        vr_buffer_ensure_size(out_buffer,
                              out_buffer->length +
                              (length + 3) / 4 * 3 + 2);

        uint8_t *out = out_buffer->data + out_buffer->length;
        const uint8_t *in;
        int ch_value;

        if (data->n_padding > 0)
                return handle_padding(data, in_buffer, length);

        for (in = in_buffer; in - in_buffer < length; in++) {
                ch_value = alphabet_value(*in);

                if (ch_value >= 0) {
                        data->value = (data->value << 6) | ch_value;

                        if (++data->n_chars >= 4) {
                                *(out++) = data->value >> 16;
                                *(out++) = data->value >> 8;
                                *(out++) = data->value;
                                data->n_chars = 0;
                        }
                } else if (*in == '=') {
                        if (!handle_padding(data,
                                            in,
                                            in_buffer + length - in))
                                return false;

                        break;
                }
        }

        out_buffer->length = out - out_buffer->data;

        return true;
}

bool
vr_base64_decode_end(struct vr_base64_data *data,
                     struct vr_buffer *out_buffer)
{
        switch (data->n_padding) {
        case 0:
                if (data->n_chars != 0)
                        return false;
                return true;
        case 1:
                if (data->n_chars != 3 ||
                    (data->value & 3) != 0)
                        return false;

                vr_buffer_append_c(out_buffer, data->value >> 10);
                vr_buffer_append_c(out_buffer, data->value >> 2);
                return true;
        case 2:
                if (data->n_chars != 2 ||
                    (data->value & 15) != 0)
                        return false;
                vr_buffer_append_c(out_buffer, data->value >> 4);
                return true;
        }

        vr_fatal("Internal base-64 decode error");
}
