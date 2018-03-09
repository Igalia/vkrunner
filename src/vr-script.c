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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "vr-script.h"
#include "vr-list.h"
#include "vr-util.h"
#include "vr-buffer.h"
#include "vr-error-message.h"

struct load_state {
        const char *filename;
        int line_num;
        struct vr_script *script;
        struct vr_buffer buffer;
        char *line;
        size_t len;
        ssize_t nread;
        int current_stage;
};

static const char *
stage_names[VR_SCRIPT_N_STAGES] = {
        "vertex shader",
        "tessellation control shader",
        "tessellation evaluation shader",
        "geometry shader",
        "fragment shader",
        "compute shader",
};

static bool
end_section(struct load_state *data)
{
        struct vr_script_shader *shader;

        if (data->current_stage == -1)
                return true;

        shader = vr_alloc(sizeof *shader + data->buffer.length);
        shader->length = data->buffer.length;
        memcpy(shader->source, data->buffer.data, shader->length);

        vr_list_insert(data->script->stages[data->current_stage].prev,
                       &shader->link);

        data->current_stage = -1;

        return true;
}

static bool
process_section_header(struct load_state *data)
{
        const char *end;
        int stage;

        if (!end_section(data))
                return false;

        end = strchr(data->line, ']');
        if (end == NULL) {
                vr_error_message("%s:%i: Missing ']'",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        for (stage = 0; stage < VR_SCRIPT_N_STAGES; stage++) {
                if (end - data->line - 1 ==
                    strlen(stage_names[stage]) &&
                    !memcmp(data->line + 1,
                            stage_names[stage],
                            end - data->line - 1))
                        goto found_stage;
        }

        vr_error_message("%s:%i: Unknown stage “%.*s”",
                         data->filename,
                         data->line_num,
                         (int) (end - data->line - 1),
                         data->line + 1);
        return false;

found_stage:
        data->current_stage = stage;
        data->buffer.length = 0;
        return true;
}

static bool
process_line(struct load_state *data)
{
        if (data->line[0] == '[')
                return process_section_header(data);

        vr_buffer_append(&data->buffer, data->line, data->nread);

        return true;
}

static struct vr_script *
load_script_from_stream(const char *filename,
                        FILE *f)
{
        struct load_state data = {
                .filename = filename,
                .line_num = 1,
                .script = vr_calloc(sizeof (struct vr_script)),
                .line = NULL,
                .len = 0,
                .buffer = VR_BUFFER_STATIC_INIT,
                .current_stage = -1
        };
        bool res = true;
        int stage;

        for (stage = 0; stage < VR_SCRIPT_N_STAGES; stage++)
                vr_list_init(&data.script->stages[stage]);

        do {
                data.nread = getline(&data.line, &data.len, f);
                if (data.nread == -1)
                        break;

                res = process_line(&data);

                data.line_num++;
        } while (res);

        if (res)
                res = end_section(&data);

        vr_buffer_destroy(&data.buffer);
        free(data.line);

        if (res) {
                return data.script;
        } else {
                vr_script_free(data.script);
                return NULL;
        }
}

struct vr_script *
vr_script_load(const char *filename)
{
        struct vr_script *script;
        FILE *f = fopen(filename, "r");

        if (f == NULL) {
                vr_error_message("%s: %s", filename, strerror(errno));
                return NULL;
        }

        script = load_script_from_stream(filename, f);

        fclose(f);

        return script;
}

void
vr_script_free(struct vr_script *script)
{
        int stage;
        struct vr_script_shader *shader, *tmp;

        for (stage = 0; stage < VR_SCRIPT_N_STAGES; stage++) {
                vr_list_for_each_safe(shader,
                                      tmp,
                                      &script->stages[stage],
                                      link) {
                        vr_free(shader);
                }
        }

        vr_free(script);
}
