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
#include <ctype.h>

#include "vr-script.h"
#include "vr-list.h"
#include "vr-util.h"
#include "vr-buffer.h"
#include "vr-error-message.h"

enum section {
        SECTION_NONE,
        SECTION_SHADER,
        SECTION_TEST
};

struct load_state {
        const char *filename;
        int line_num;
        struct vr_script *script;
        struct vr_buffer buffer;
        char *line;
        size_t len;
        ssize_t nread;
        int current_stage;
        enum section current_section;
        struct vr_buffer commands;
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

static void
end_shader(struct load_state *data)
{
        struct vr_script_shader *shader;

        shader = vr_alloc(sizeof *shader + data->buffer.length);
        shader->length = data->buffer.length;
        memcpy(shader->source, data->buffer.data, shader->length);

        vr_list_insert(data->script->stages[data->current_stage].prev,
                       &shader->link);
}

static bool
end_section(struct load_state *data)
{
        switch (data->current_section) {
        case SECTION_NONE:
                break;

        case SECTION_SHADER:
                end_shader(data);
                break;

        case SECTION_TEST:
                break;
        }

        data->current_section = SECTION_NONE;

        return true;
}

static bool
is_string(const char *string,
          const char *start,
          const char *end)
{
        return (end - start == strlen(string) &&
                !memcmp(start, string, end - start));
}

static bool
looking_at(const char **p,
           const char *string)
{
        int len = strlen(string);

        if (strncmp(*p, string, len) == 0) {
                *p += len;
                return true;
        }

        return false;
}

static bool
is_end(const char *p)
{
        while (*p && isspace(*p))
                p++;

        return *p == '\0';
}

static bool
parse_floats(const char **p,
             float *out,
             int n_floats)
{
        char *tail;

        for (int i = 0; i < n_floats; i++) {
                while (isspace(**p))
                        (*p)++;
                errno = 0;
                *(out++) = strtof(*p, &tail);
                if (errno != 0 || tail == *p)
                        return false;
                *p = tail;
        }

        return true;
}

static bool
process_test_line(struct load_state *data)
{
        const char *p = data->line;

        while (*p && isspace(*p))
                p++;

        if (*p == '#' || *p == '\0')
                return true;

        vr_buffer_set_length(&data->commands,
                             data->commands.length +
                             sizeof (struct vr_script_command));
        struct vr_script_command *command =
                (struct vr_script_command *)
                (data->commands.data +
                 data->commands.length -
                 sizeof (struct vr_script_command));

        command->line_num = data->line_num;

        if (looking_at(&p, "draw rect ")) {
                if (!parse_floats(&p, &command->draw_rect.x, 4) ||
                    !is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_DRAW_RECT;
                return true;
        }

error:
        vr_error_message("%s:%i: Invalid test command",
                         data->filename,
                         data->line_num);
        return false;
}

static bool
process_section_header(struct load_state *data)
{
        if (!end_section(data))
                return false;

        const char *start = data->line + 1;
        const char *end = strchr(start, ']');
        if (end == NULL) {
                vr_error_message("%s:%i: Missing ']'",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        for (int stage = 0; stage < VR_SCRIPT_N_STAGES; stage++) {
                if (is_string(stage_names[stage], start, end)) {
                        data->current_section = SECTION_SHADER;
                        data->current_stage = stage;
                        data->buffer.length = 0;
                        return true;
                }
        }

        if (is_string("test", start, end)) {
                data->current_section = SECTION_TEST;
                return true;
        }

        vr_error_message("%s:%i: Unknown section “%.*s”",
                         data->filename,
                         data->line_num,
                         (int) (end - start),
                         start);
        return false;
}

static bool
process_line(struct load_state *data)
{
        if (data->line[0] == '[')
                return process_section_header(data);

        switch (data->current_section) {
        case SECTION_NONE:
                return true;

        case SECTION_SHADER:
                vr_buffer_append(&data->buffer, data->line, data->nread);
                return true;

        case SECTION_TEST:
                return process_test_line(data);
        }

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
                .commands = VR_BUFFER_STATIC_INIT,
                .current_stage = -1,
                .current_section = SECTION_NONE
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

        data.script->commands = vr_memdup(data.commands.data,
                                          data.commands.length);
        data.script->n_commands = (data.commands.length /
                                   sizeof (struct vr_script_command));

        vr_buffer_destroy(&data.commands);
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

        vr_free(script->commands);

        vr_free(script);
}
