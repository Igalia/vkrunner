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
#include <limits.h>

#include "vr-script.h"
#include "vr-list.h"
#include "vr-util.h"
#include "vr-buffer.h"
#include "vr-error-message.h"
#include "vr-feature-offsets.h"
#include "vr-window.h"

enum section {
        SECTION_NONE,
        SECTION_REQUIRE,
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
        enum vr_script_shader_stage current_stage;
        enum vr_script_source_type current_source_type;
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
        shader->source_type = data->current_source_type;
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

        case SECTION_REQUIRE:
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
             int n_floats,
             const char *sep)
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

                if (sep && i < n_floats - 1) {
                        while (isspace(**p))
                                (*p)++;
                        if (!looking_at(p, sep))
                                return false;
                }
        }

        return true;
}

static bool
parse_doubles(const char **p,
              double *out,
              int n_doubles,
              const char *sep)
{
        char *tail;

        for (int i = 0; i < n_doubles; i++) {
                while (isspace(**p))
                        (*p)++;

                errno = 0;
                *(out++) = strtod(*p, &tail);
                if (errno != 0 || tail == *p)
                        return false;
                *p = tail;

                if (sep && i < n_doubles - 1) {
                        while (isspace(**p))
                                (*p)++;
                        if (!looking_at(p, sep))
                                return false;
                }
        }

        return true;
}

static bool
parse_ints(const char **p,
           int *out,
           int n_ints,
           const char *sep)
{
        long v;
        char *tail;

        for (int i = 0; i < n_ints; i++) {
                while (isspace(**p))
                        (*p)++;

                errno = 0;
                v = strtol(*p, &tail, 10);
                if (errno != 0 || tail == *p ||
                    v < INT_MIN || v > INT_MAX)
                        return false;
                *(out++) = (int) v;
                *p = tail;

                if (sep && i < n_ints - 1) {
                        while (isspace(**p))
                                (*p)++;
                        if (!looking_at(p, sep))
                                return false;
                }
        }

        return true;
}

static bool
parse_size_t(const char **p,
             size_t *out)
{
        unsigned long v;
        char *tail;

        errno = 0;
        v = strtoul(*p, &tail, 10);
        if (errno != 0 || tail == *p || v > SIZE_MAX)
                return false;
        *out = v;
        *p = tail;

        return true;
}

static bool
parse_value_type(const char **p,
                 enum vr_script_type *type)
{
        static const struct {
                const char *name;
                enum vr_script_type type;
        } types[] = {
                { "int ", VR_SCRIPT_TYPE_INT },
                { "float ", VR_SCRIPT_TYPE_FLOAT },
                { "double ", VR_SCRIPT_TYPE_DOUBLE },
                { "vec2 ", VR_SCRIPT_TYPE_VEC2 },
                { "vec3 ", VR_SCRIPT_TYPE_VEC3 },
                { "vec4 ", VR_SCRIPT_TYPE_VEC4 },
                { "dvec2 ", VR_SCRIPT_TYPE_DVEC2 },
                { "dvec3 ", VR_SCRIPT_TYPE_DVEC3 },
                { "dvec4 ", VR_SCRIPT_TYPE_DVEC4 },
                { "ivec2 ", VR_SCRIPT_TYPE_IVEC2 },
                { "ivec3 ", VR_SCRIPT_TYPE_IVEC3 },
                { "ivec4 ", VR_SCRIPT_TYPE_IVEC4 },
        };

        for (int i = 0; i < VR_N_ELEMENTS(types); i++) {
                if (looking_at(p, types[i].name)) {
                        *type = types[i].type;
                        return true;
                }
        }

        return false;
}

static bool
parse_value(const char **p,
            struct vr_script_value *value)
{
        switch (value->type) {
        case VR_SCRIPT_TYPE_INT:
                return parse_ints(p, &value->i, 1, NULL);
        case VR_SCRIPT_TYPE_FLOAT:
                return parse_floats(p, &value->f, 1, NULL);
        case VR_SCRIPT_TYPE_DOUBLE:
                return parse_doubles(p, &value->d, 1, NULL);
        case VR_SCRIPT_TYPE_VEC2:
                return parse_floats(p, value->vec, 2, NULL);
        case VR_SCRIPT_TYPE_VEC3:
                return parse_floats(p, value->vec, 3, NULL);
        case VR_SCRIPT_TYPE_VEC4:
                return parse_floats(p, value->vec, 4, NULL);
        case VR_SCRIPT_TYPE_DVEC2:
                return parse_doubles(p, value->dvec, 2, NULL);
        case VR_SCRIPT_TYPE_DVEC3:
                return parse_doubles(p, value->dvec, 3, NULL);
        case VR_SCRIPT_TYPE_DVEC4:
                return parse_doubles(p, value->dvec, 4, NULL);
        case VR_SCRIPT_TYPE_IVEC2:
                return parse_ints(p, value->ivec, 2, NULL);
        case VR_SCRIPT_TYPE_IVEC3:
                return parse_ints(p, value->ivec, 3, NULL);
        case VR_SCRIPT_TYPE_IVEC4:
                return parse_ints(p, value->ivec, 4, NULL);
        }

        vr_fatal("should not be reached");
}

static bool
process_require_line(struct load_state *data)
{
        const char *start = data->line, *p;

        while (*start && isspace(*start))
                start++;

        if (*start == '#' || *start == '\0')
                return true;

        for (int i = 0; vr_feature_offsets[i].name; i++) {
                p = start;
                if (!looking_at(&p, vr_feature_offsets[i].name) || !is_end(p))
                        continue;
                *(VkBool32 *) ((uint8_t *) &data->script->required_features +
                               vr_feature_offsets[i].offset) = true;
                return true;
        }

        vr_error_message("%s:%i: Invalid require line",
                         data->filename,
                         data->line_num);

        return false;
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
                if (!parse_floats(&p, &command->draw_rect.x, 4, NULL) ||
                    !is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_DRAW_RECT;
                return true;
        }

        if (looking_at(&p, "probe rect rgba ")) {
                while (isspace(*p))
                        p++;
                if (*(p++) != '(')
                        goto error;
                if (!parse_ints(&p, &command->probe_rect.x, 4, ","))
                        goto error;
                while (isspace(*p))
                        p++;
                if (*(p++) != ')')
                        goto error;
                while (isspace(*p))
                        p++;
                if (*(p++) != '(')
                        goto error;
                if (!parse_floats(&p, command->probe_rect.color, 4, ","))
                        goto error;
                while (isspace(*p))
                        p++;
                if (*p != ')' || !is_end(p + 1))
                        goto error;
                command->op = VR_SCRIPT_OP_PROBE_RECT_RGBA;
                return true;
        }

        if (looking_at(&p, "probe all rgba ")) {
                if (!parse_floats(&p, command->probe_rect.color, 4, NULL))
                        goto error;
                if (!is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_PROBE_RECT_RGBA;
                command->probe_rect.x = 0;
                command->probe_rect.y = 0;
                command->probe_rect.w = VR_WINDOW_WIDTH;
                command->probe_rect.h = VR_WINDOW_HEIGHT;
                return true;
        }

        if (looking_at(&p, "uniform ")) {
                while (isspace(*p))
                        p++;
                if (!parse_value_type(&p,
                                      &command->set_push_constant.value.type))
                        goto error;
                if (!parse_size_t(&p, &command->set_push_constant.offset))
                        goto error;
                if (!parse_value(&p, &command->set_push_constant.value))
                        goto error;
                if (!is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_SET_PUSH_CONSTANT;
                return true;
        }

        if (looking_at(&p, "clear color ")) {
                if (!parse_floats(&p, command->clear_color.color, 4, NULL))
                        goto error;
                if (!is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_CLEAR_COLOR;
                return true;
        }

        if (looking_at(&p, "clear")) {
                if (!is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_CLEAR;
                return true;
        }

error:
        vr_error_message("%s:%i: Invalid test command",
                         data->filename,
                         data->line_num);
        return false;
}

static bool
is_stage_section(struct load_state *data,
                 const char *start,
                 const char *end)
{
        int stage;

        for (stage = 0; stage < VR_SCRIPT_N_STAGES; stage++) {
                if (is_string(stage_names[stage], start, end)) {
                        data->current_source_type = VR_SCRIPT_SOURCE_TYPE_GLSL;
                        goto found;
                }
        }

        if (end - start <= 6 || memcmp(" spirv", end - 6, 6))
                return false;

        end -= 6;

        for (stage = 0; stage < VR_SCRIPT_N_STAGES; stage++) {
                if (is_string(stage_names[stage], start, end)) {
                        data->current_source_type = VR_SCRIPT_SOURCE_TYPE_SPIRV;
                        goto found;
                }
        }

        return false;

found:
        data->current_section = SECTION_SHADER;
        data->current_stage = stage;
        data->buffer.length = 0;
        return true;
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

        if (is_stage_section(data, start, end)) {
                const struct vr_script *script = data->script;
                if (data->current_source_type == VR_SCRIPT_SOURCE_TYPE_SPIRV &&
                    !vr_list_empty(&script->stages[data->current_stage])) {
                        vr_error_message("%s:%i: SPIR-V source can not be "
                                         "linked with other shaders in the "
                                         "same stage",
                                         data->filename,
                                         data->line_num);
                        return false;
                }

                return true;
        }

        if (is_string("require", start, end)) {
                data->current_section = SECTION_REQUIRE;
                return true;
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

        case SECTION_REQUIRE:
                return process_require_line(data);

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

        data.script->filename = vr_strdup(filename);

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

        vr_free(script->filename);

        vr_free(script->commands);

        vr_free(script);
}

size_t
vr_script_type_size(enum vr_script_type type)
{
        switch (type) {
        case VR_SCRIPT_TYPE_INT:
        case VR_SCRIPT_TYPE_FLOAT:
                return 4;
        case VR_SCRIPT_TYPE_DOUBLE:
                return 8;
        case VR_SCRIPT_TYPE_VEC2:
        case VR_SCRIPT_TYPE_IVEC2:
                return 4 * 2;
        case VR_SCRIPT_TYPE_VEC3:
        case VR_SCRIPT_TYPE_IVEC3:
                return 4 * 3;
        case VR_SCRIPT_TYPE_VEC4:
        case VR_SCRIPT_TYPE_IVEC4:
                return 4 * 4;
        case VR_SCRIPT_TYPE_DVEC2:
                return 8 * 2;
        case VR_SCRIPT_TYPE_DVEC3:
                return 8 * 3;
        case VR_SCRIPT_TYPE_DVEC4:
                return 8 * 4;
        }

        vr_fatal("should not be reached");
}
