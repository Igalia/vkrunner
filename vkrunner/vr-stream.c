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

#include "vr-stream.h"
#include "vr-util.h"

void
vr_stream_init_string(struct vr_stream *stream,
                      const char *string)
{
        stream->type = VR_STREAM_TYPE_STRING;
        stream->string = string;
        stream->end = string + strlen(string);
}

void
vr_stream_init_file(struct vr_stream *stream,
                    FILE *file)
{
        stream->type = VR_STREAM_TYPE_FILE;
        stream->file = file;
}

static bool
read_line_from_string(struct vr_stream *stream,
                      struct vr_buffer *buffer)
{
        if (stream->string >= stream->end)
                return false;

        const char *end = memchr(stream->string,
                                 '\n',
                                 stream->end - stream->string);

        if (end == NULL) {
                vr_buffer_append(buffer,
                                 stream->string,
                                 stream->end - stream->string);
                stream->string = stream->end;
        } else {
                vr_buffer_append(buffer,
                                 stream->string,
                                 end - stream->string + 1);
                stream->string = end + 1;
        }

        return true;
}

static bool
read_line_from_file(FILE *f,
                    struct vr_buffer *buffer)
{
        bool got_something = false;

        while (true) {
                vr_buffer_ensure_size(buffer, buffer->length + 128);

                char *read_start = (char *) buffer->data + buffer->length;

                if (!fgets(read_start, buffer->size - buffer->length, f))
                        break;

                got_something = true;

                buffer->length += strlen(read_start);

                if (strchr(read_start, '\n'))
                        break;
        }

        return got_something;
}

static bool
raw_read_line(struct vr_stream *stream,
              struct vr_buffer *buffer)
{
        switch (stream->type) {
        case VR_STREAM_TYPE_STRING:
                return read_line_from_string(stream, buffer);
        case VR_STREAM_TYPE_FILE:
                return read_line_from_file(stream->file, buffer);
        }

        vr_fatal("Unexpected stream type");
}

bool
vr_stream_read_line(struct vr_stream *stream,
                    struct vr_buffer *buffer)
{
        bool got_something = false;

        buffer->length = 0;

        while (true) {
                size_t old_length = buffer->length;

                if (!raw_read_line(stream, buffer))
                        break;

                got_something = true;

                if (buffer->length >= old_length + 2) {
                        if (!memcmp(buffer->data + buffer->length - 2,
                                    "\\\n",
                                    2)) {
                                buffer->length -= 2;
                                continue;
                        }
                        if (buffer->length >= old_length + 3 &&
                            !memcmp(buffer->data + buffer->length - 3,
                                    "\\\r\n",
                                    3)) {
                                buffer->length -= 3;
                                continue;
                        }
                }

                break;
        }

        if (!got_something)
                return false;

        vr_buffer_append_c(buffer, '\0');
        buffer->length--;

        return true;
}
