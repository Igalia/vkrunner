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
#include <assert.h>

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
        SECTION_VERTEX_DATA,
        SECTION_TEST
};

struct load_state {
        const char *filename;
        int line_num;
        struct vr_script *script;
        struct vr_buffer buffer;
        struct vr_buffer line;
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

static const char
vertex_shader_passthrough[] =
        "#version 430\n"
        "\n"
        "layout(location = 0) in vec4 piglit_vertex;\n"
        "\n"
        "void\n"
        "main()\n"
        "{\n"
        "        gl_Position = piglit_vertex;\n"
        "}\n";

static void
add_shader(struct load_state *data,
           enum vr_script_shader_stage stage,
           enum vr_script_source_type source_type,
           size_t length,
           const char *source)
{
        struct vr_script_shader *shader;

        shader = vr_alloc(sizeof *shader + length);
        shader->length = length;
        shader->source_type = source_type;
        memcpy(shader->source, source, length);

        vr_list_insert(data->script->stages[stage].prev, &shader->link);
}

static void
end_shader(struct load_state *data)
{
        add_shader(data,
                   data->current_stage,
                   data->current_source_type,
                   data->buffer.length,
                   (const char *) data->buffer.data);
}

static bool
end_vertex_data(struct load_state *data)
{
        data->script->vertex_data = vr_vbo_parse((const char *)
                                                 data->buffer.data,
                                                 data->buffer.length);
        return data->script->vertex_data != NULL;
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

        case SECTION_VERTEX_DATA:
                return end_vertex_data(data);

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
process_none_line(struct load_state *data)
{
        const char *start = (char *) data->line.data;

        while (*start && isspace(*start))
                start++;

        if (*start != '#' && *start != '\0') {
                vr_error_message("%s:%i expected empty line",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        return true;
}

static bool
process_require_line(struct load_state *data)
{
        const char *start = (char *) data->line.data, *p = start;

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

        if (looking_at(&p, "framebuffer ")) {
                while (isspace(*p))
                        p++;
                const char *end = p;
                while (*end && !isspace(*end))
                        end++;

                if (is_end(end)) {
                        char *format_name = vr_strndup(p, end - p);
                        const struct vr_format *format =
                                vr_format_lookup_by_name(format_name);
                        bool ret;

                        if (format == NULL) {
                                vr_error_message("%s:%i: Unknown format: %s",
                                                 data->filename,
                                                 data->line_num,
                                                 format_name);
                                ret = false;
                        } else {
                                data->script->framebuffer_format = format;
                                ret = true;
                        }

                        vr_free(format_name);

                        return ret;
                }
        }

        vr_error_message("%s:%i: Invalid require line",
                         data->filename,
                         data->line_num);

        return false;
}

static bool
process_probe_command(const char **p,
                      struct vr_script_command *command)
{
        bool relative = false;
        enum { POINT, RECT, ALL } region_type = POINT;
        int n_components;

        if (looking_at(p, "relative "))
                relative = true;

        if (!looking_at(p, "probe "))
                return false;

        if (looking_at(p, "rect "))
                region_type = RECT;
        else if (looking_at(p, "all "))
                region_type = ALL;

        if (looking_at(p, "rgb "))
                n_components = 3;
        else if (looking_at(p, "rgba "))
                n_components = 4;
        else
                return false;

        command->op = VR_SCRIPT_OP_PROBE_RECT;
        command->probe_rect.n_components = n_components;

        if (region_type == ALL) {
                if (relative)
                        return false;
                if (!parse_doubles(p,
                                   command->probe_rect.color,
                                   n_components,
                                   NULL))
                        return false;
                if (!is_end(*p))
                        return false;
                command->probe_rect.x = 0;
                command->probe_rect.y = 0;
                command->probe_rect.w = VR_WINDOW_WIDTH;
                command->probe_rect.h = VR_WINDOW_HEIGHT;
                return true;
        }

        while (isspace(**p))
                (*p)++;
        if (**p != '(')
                return false;
        (*p)++;

        if (region_type == POINT) {
                if (relative) {
                        float rel_pos[2];
                        if (!parse_floats(p, rel_pos, 2, ","))
                                return false;
                        command->probe_rect.x = rel_pos[0] * VR_WINDOW_WIDTH;
                        command->probe_rect.y = rel_pos[1] * VR_WINDOW_HEIGHT;
                } else if (!parse_ints(p, &command->probe_rect.x, 2, ",")) {
                        return false;
                }
                command->probe_rect.w = 1;
                command->probe_rect.h = 1;
        } else {
                assert(region_type == RECT);

                if (relative) {
                        float rel_pos[4];
                        if (!parse_floats(p, rel_pos, 4, ","))
                                return false;
                        command->probe_rect.x = rel_pos[0] * VR_WINDOW_WIDTH;
                        command->probe_rect.y = rel_pos[1] * VR_WINDOW_HEIGHT;
                        command->probe_rect.w = rel_pos[2] * VR_WINDOW_WIDTH;
                        command->probe_rect.h = rel_pos[3] * VR_WINDOW_HEIGHT;
                } else if (!parse_ints(p, &command->probe_rect.x, 4, ",")) {
                        return false;
                }
        }

        while (isspace(**p))
                (*p)++;
        if (**p != ')')
                return false;
        (*p)++;

        while (isspace(**p))
                (*p)++;
        if (**p != '(')
                return false;
        (*p)++;

        if (!parse_doubles(p, command->probe_rect.color, n_components, ","))
                return false;

        while (isspace(**p))
                (*p)++;
        if (**p != ')')
                return false;
        (*p)++;

        if (!is_end(*p))
                return false;

        return true;
}

static bool
process_draw_arrays_command(struct load_state *data,
                            const char *p,
                            struct vr_script_command *command)
{
        int args[3];
        int n_args;

        if (looking_at(&p, "instanced ")) {
                n_args = 3;
        } else {
                n_args = 2;
                args[2] = 1;
        }

        static const struct {
                const char *name;
                VkPrimitiveTopology topology;
        } topologies[] = {
                /* GL names used in Piglit */
                { "GL_POINTS", VK_PRIMITIVE_TOPOLOGY_POINT_LIST },
                { "GL_LINES", VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
                { "GL_LINE_STRIP", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP },
                { "GL_TRIANGLES", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST },
                { "GL_TRIANGLE_STRIP", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP },
                { "GL_TRIANGLE_FAN", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN },
                { "GL_LINES_ADJACENCY",
                  VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY },
                { "GL_LINE_STRIP_ADJACENCY",
                  VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY },
                { "GL_TRIANGLES_ADJACENCY",
                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY },
                { "GL_TRIANGLE_STRIP_ADJACENCY",
                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY },
                { "GL_PATCHES", VK_PRIMITIVE_TOPOLOGY_PATCH_LIST },
                /* Vulkan names */
#define vkname(x) { VR_STRINGIFY(x), VK_PRIMITIVE_TOPOLOGY_ ## x }
                vkname(POINT_LIST),
                vkname(LINE_LIST),
                vkname(LINE_STRIP),
                vkname(TRIANGLE_LIST),
                vkname(TRIANGLE_STRIP),
                vkname(TRIANGLE_FAN),
                vkname(LINE_LIST_WITH_ADJACENCY),
                vkname(LINE_STRIP_WITH_ADJACENCY),
                vkname(TRIANGLE_LIST_WITH_ADJACENCY),
                vkname(TRIANGLE_STRIP_WITH_ADJACENCY),
                vkname(PATCH_LIST),
#undef vkname
        };

        for (int i = 0; i < VR_N_ELEMENTS(topologies); i++) {
                if (looking_at(&p, topologies[i].name)) {
                        command->draw_arrays.topology = topologies[i].topology;
                        goto found_topology;
                }
        }

        vr_error_message("%s:%i: Unknown topology in draw arrays command",
                         data->filename,
                         data->line_num);
        return false;

found_topology:
        if (!parse_ints(&p, args, n_args, NULL) ||
            !is_end(p)) {
                vr_error_message("%s:%i: Invalid draw arrays command",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        command->op = VR_SCRIPT_OP_DRAW_ARRAYS;
        command->draw_arrays.first_vertex = args[0];
        command->draw_arrays.vertex_count = args[1];
        command->draw_arrays.first_instance = 0;
        command->draw_arrays.instance_count = args[2];

        return true;
}

static bool
process_test_line(struct load_state *data)
{
        const char *p = (char *) data->line.data;

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

        if (process_probe_command(&p, command))
                return true;

        if (looking_at(&p, "draw arrays "))
                return process_draw_arrays_command(data, p, command);

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

        const char *start = (char *) data->line.data + 1;
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

        if (is_string("vertex shader passthrough", start, end)) {
                data->current_section = SECTION_NONE;
                add_shader(data,
                           VR_SCRIPT_SHADER_STAGE_VERTEX,
                           VR_SCRIPT_SOURCE_TYPE_GLSL,
                           (sizeof vertex_shader_passthrough) - 1,
                           vertex_shader_passthrough);
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

        if (is_string("vertex data", start, end)) {
                if (data->script->vertex_data) {
                        vr_error_message("%s:%i: Duplicate vertex data section",
                                         data->filename,
                                         data->line_num);
                        return false;
                }
                data->current_section = SECTION_VERTEX_DATA;
                data->buffer.length = 0;
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
        if (*data->line.data == '[')
                return process_section_header(data);

        switch (data->current_section) {
        case SECTION_NONE:
                return process_none_line(data);

        case SECTION_REQUIRE:
                return process_require_line(data);

        case SECTION_SHADER:
        case SECTION_VERTEX_DATA:
                vr_buffer_append(&data->buffer,
                                 data->line.data,
                                 data->line.length);
                return true;

        case SECTION_TEST:
                return process_test_line(data);
        }

        return true;
}

static bool
read_line(FILE *f,
          struct vr_buffer *buffer)
{
        buffer->length = 0;

        while (true) {
                vr_buffer_ensure_size(buffer, buffer->length + 128);

                char *read_start = (char *) buffer->data + buffer->length;

                if (!fgets(read_start, buffer->size - buffer->length, f))
                        break;

                buffer->length += strlen(read_start);

                if (strchr(read_start, '\n'))
                        break;
        }

        return buffer->length > 0;
}

static struct vr_script *
load_script_from_stream(const char *filename,
                        FILE *f)
{
        struct load_state data = {
                .filename = filename,
                .line_num = 1,
                .script = vr_calloc(sizeof (struct vr_script)),
                .line = VR_BUFFER_STATIC_INIT,
                .buffer = VR_BUFFER_STATIC_INIT,
                .commands = VR_BUFFER_STATIC_INIT,
                .current_stage = -1,
                .current_section = SECTION_NONE
        };
        bool res = true;
        int stage;

        data.script->filename = vr_strdup(filename);
        data.script->framebuffer_format =
                vr_format_lookup_by_vk_format(VK_FORMAT_B8G8R8A8_UNORM);
        assert(data.script->framebuffer_format != NULL);

        for (stage = 0; stage < VR_SCRIPT_N_STAGES; stage++)
                vr_list_init(&data.script->stages[stage]);

        do {
                if (!read_line(f, &data.line))
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
        vr_buffer_destroy(&data.line);

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

        if (script->vertex_data)
                vr_vbo_free(script->vertex_data);

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
