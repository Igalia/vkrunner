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

#include "vr-source-private.h"

#include <string.h>

static struct vr_source *
source_new_with_type(enum vr_source_type type,
                     const char *string)
{
        size_t length = strlen(string);
        struct vr_source *source = vr_alloc(sizeof *source + length + 1);

        source->type = type;
        vr_list_init(&source->token_replacements);
        memcpy(source->string, string, length + 1);

        return source;
}

struct vr_source *
vr_source_from_string(const char *string)
{
        return source_new_with_type(VR_SOURCE_TYPE_STRING, string);
}

struct vr_source *
vr_source_from_file(const char *filename)
{
        return source_new_with_type(VR_SOURCE_TYPE_FILE, filename);
}

void
vr_source_add_token_replacement(struct vr_source *source,
                                const char *token,
                                const char *replacement)
{
        struct vr_source_token_replacement *tr = vr_calloc(sizeof *tr);

        tr->token = vr_strdup(token);
        tr->replacement = vr_strdup(replacement);

        vr_list_insert(source->token_replacements.prev, &tr->link);
}

static void
free_token_replacements(struct vr_source *source)
{
        struct vr_source_token_replacement *tr, *tmp;

        vr_list_for_each_safe(tr, tmp, &source->token_replacements, link) {
                vr_free(tr->token);
                vr_free(tr->replacement);
                vr_free(tr);
        }
}

char *
vr_source_get_filename(const struct vr_source *source)
{
        switch (source->type) {
        case VR_SOURCE_TYPE_FILE:
                return vr_strdup(source->string);
        case VR_SOURCE_TYPE_STRING:
                return vr_strdup("(string source)");
        }

        vr_fatal("unexpected source type");
}

void
vr_source_free(struct vr_source *source)
{
        free_token_replacements(source);

        vr_free(source);
}
