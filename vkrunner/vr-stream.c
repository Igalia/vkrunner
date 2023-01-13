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
#include "vr-buffer.h"
#include "vr-source-private.h"

#include <string.h>

enum vr_stream_type {
        VR_STREAM_TYPE_STRING,
        VR_STREAM_TYPE_FILE
};

struct vr_stream {
        enum vr_stream_type type;

        const struct vr_source *source;

        union {
                FILE *file;
                struct {
                        const char *string;
                        const char *end;
                };
        };

        struct vr_buffer buffer;

        int line_num;
        int next_line_num;
};

struct vr_stream *
vr_stream_new(const struct vr_source *source)
{
        struct vr_stream *stream = vr_calloc(sizeof *stream);

        switch (source->type) {
        case VR_SOURCE_TYPE_FILE:
                stream->type = VR_STREAM_TYPE_FILE;

                char *filename = vr_source_get_filename(source);
                stream->file = fopen(filename, "rt");
                vr_free(filename);

                if (stream->file == NULL) {
                        vr_free(stream);
                        return NULL;
                }

                break;

        case VR_SOURCE_TYPE_STRING:
                stream->type = VR_STREAM_TYPE_STRING;
                stream->string = source->string;
                stream->end = source->string + strlen(source->string);
                break;
        }

        stream->source = source;
        vr_buffer_init(&stream->buffer);
        stream->next_line_num = 1;

        return stream;
}

int
vr_stream_get_line_num(const struct vr_stream *stream)
{
        return stream->line_num;
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

static bool
find_replacement(const struct vr_list *replacements,
                 struct vr_buffer *line,
                 int pos)
{
        const struct vr_source_token_replacement *tr;

        vr_list_for_each(tr, replacements, link) {
                int len = strlen(tr->token);

                if (pos + len <= line->length &&
                    !memcmp(line->data + pos, tr->token, len)) {
                        int repl_len = strlen(tr->replacement);
                        int new_line_len = line->length + repl_len - len;
                        /* The extra “1” is to preserve the null terminator */
                        vr_buffer_ensure_size(line, new_line_len + 1);
                        memmove(line->data + pos + repl_len,
                                line->data + pos + len,
                                line->length - pos - len + 1);
                        memcpy(line->data + pos, tr->replacement, repl_len);

                        vr_buffer_set_length(line, new_line_len);

                        return true;
                }
        }

        return false;
}

static bool
process_token_replacements(struct vr_stream *stream)
{
        int count = 0;

        for (int i = 0; i < stream->buffer.length; i++) {
                while (find_replacement(&stream->source->token_replacements,
                                        &stream->buffer,
                                        i)) {
                        count++;

                        if (count > 1000) {
                                char *filename =
                                        vr_source_get_filename(stream->source);
                                fprintf(stderr,
                                        "%s:%i: infinite recursion suspected "
                                        "while replacing tokens\n",
                                        filename,
                                        stream->line_num);
                                vr_free(filename);
                                return false;
                        }
                }
        }

        return true;
}

bool
vr_stream_read_line(struct vr_stream *stream,
                    const char **line_out,
                    size_t *line_length_out)
{
        stream->line_num = stream->next_line_num;

        stream->buffer.length = 0;

        while (true) {
                size_t old_length = stream->buffer.length;

                if (!raw_read_line(stream, &stream->buffer))
                        break;

                stream->next_line_num++;

                if (stream->buffer.length >= old_length + 2) {
                        if (!memcmp(stream->buffer.data +
                                    stream->buffer.length -
                                    2,
                                    "\\\n",
                                    2)) {
                                stream->buffer.length -= 2;
                                continue;
                        }
                        if (stream->buffer.length >= old_length + 3 &&
                            !memcmp(stream->buffer.data +
                                    stream->buffer.length -
                                    3,
                                    "\\\r\n",
                                    3)) {
                                stream->buffer.length -= 3;
                                continue;
                        }
                }

                break;
        }

        if (stream->buffer.length > 0) {
                if (!process_token_replacements(stream))
                        return false;

                vr_buffer_append_c(&stream->buffer, '\0');
                stream->buffer.length--;

                *line_out = (const char *) stream->buffer.data;
                *line_length_out = stream->buffer.length;

                return true;
        } else {
                return false;
        }
}

void
vr_stream_free(struct vr_stream *stream)
{
        if (stream->type == VR_STREAM_TYPE_FILE)
                fclose(stream->file);

        vr_buffer_destroy(&stream->buffer);

        vr_free(stream);
}
