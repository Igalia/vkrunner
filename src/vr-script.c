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
        SECTION_COMMENT,
        SECTION_REQUIRE,
        SECTION_SHADER,
        SECTION_VERTEX_DATA,
        SECTION_INDICES,
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
        struct vr_buffer extensions;
        struct vr_pipeline_key current_key;
        struct vr_buffer indices;
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
        "               OpCapability Shader\n"
        "               OpMemoryModel Logical GLSL450\n"
        "               OpEntryPoint Vertex %main \"main\" %pos_in %pos_out\n"
        "               OpDecorate %pos_in Location 0\n"
        "               OpDecorate %pos_out BuiltIn Position\n"
        "       %void = OpTypeVoid\n"
        "  %func_type = OpTypeFunction %void\n"
        "      %float = OpTypeFloat 32\n"
        "    %v4float = OpTypeVector %float 4\n"
        "%_ptr_Input_v4float = OpTypePointer Input %v4float\n"
        "%_ptr_Output_v4float = OpTypePointer Output %v4float\n"
        "     %pos_in = OpVariable %_ptr_Input_v4float Input\n"
        "    %pos_out = OpVariable %_ptr_Output_v4float Output\n"
        "       %main = OpFunction %void None %func_type\n"
        " %main_label = OpLabel\n"
        " %pos_in_val = OpLoad %v4float %pos_in\n"
        "               OpStore %pos_out %pos_in_val\n"
        "               OpReturn\n"
        "               OpFunctionEnd\n";

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

        case SECTION_COMMENT:
                break;

        case SECTION_REQUIRE:
                break;

        case SECTION_SHADER:
                end_shader(data);
                break;

        case SECTION_VERTEX_DATA:
                return end_vertex_data(data);

        case SECTION_INDICES:
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

static int
type_columns(enum vr_script_type type)
{
        if (type >= VR_SCRIPT_TYPE_MAT2 && type <= VR_SCRIPT_TYPE_MAT4)
                return (type - VR_SCRIPT_TYPE_MAT2) / 3 + 2;
        if (type >= VR_SCRIPT_TYPE_DMAT2 && type <= VR_SCRIPT_TYPE_DMAT4)
                return (type - VR_SCRIPT_TYPE_DMAT2) / 3 + 2;
        return 1;
}

static int
type_rows(enum vr_script_type type)
{
        if (type >= VR_SCRIPT_TYPE_VEC2 && type <= VR_SCRIPT_TYPE_VEC4)
                return type - VR_SCRIPT_TYPE_VEC2 + 2;
        if (type >= VR_SCRIPT_TYPE_DVEC2 && type <= VR_SCRIPT_TYPE_DVEC4)
                return type - VR_SCRIPT_TYPE_DVEC2 + 2;
        if (type >= VR_SCRIPT_TYPE_IVEC2 && type <= VR_SCRIPT_TYPE_IVEC4)
                return type - VR_SCRIPT_TYPE_IVEC2 + 2;
        if (type >= VR_SCRIPT_TYPE_UVEC2 && type <= VR_SCRIPT_TYPE_UVEC4)
                return type - VR_SCRIPT_TYPE_UVEC2 + 2;
        if (type >= VR_SCRIPT_TYPE_I64VEC2 && type <= VR_SCRIPT_TYPE_I64VEC4)
                return type - VR_SCRIPT_TYPE_I64VEC2 + 2;
        if (type >= VR_SCRIPT_TYPE_U64VEC2 && type <= VR_SCRIPT_TYPE_U64VEC4)
                return type - VR_SCRIPT_TYPE_U64VEC2 + 2;
        if (type >= VR_SCRIPT_TYPE_MAT2 && type <= VR_SCRIPT_TYPE_MAT4)
                return (type - VR_SCRIPT_TYPE_MAT2) % 3 + 2;
        if (type >= VR_SCRIPT_TYPE_DMAT2 && type <= VR_SCRIPT_TYPE_DMAT4)
                return (type - VR_SCRIPT_TYPE_DMAT2) % 3 + 2;
        return 1;
}

/**
 * Calculates the matrix stride of a type assuming std140 rules.
 */
static size_t
type_matrix_stride(enum vr_script_type type)
{
        int component_size;

        if (type >= VR_SCRIPT_TYPE_MAT2 && type <= VR_SCRIPT_TYPE_MAT4)
                component_size = 4;
        else if (type >= VR_SCRIPT_TYPE_DMAT2 && type <= VR_SCRIPT_TYPE_DMAT4)
                component_size = 8;
        else
                vr_fatal("Matrix size requested for non-matrix type");

        int rows = type_rows(type);
        int base_alignment;

        if (rows == 3)
                base_alignment = component_size * 4;
        else
                base_alignment = component_size * rows;

        /* according to std140 the size is rounded up to a vec4 */
        return vr_align(base_alignment, 16);
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
parse_uints(const char **p,
            unsigned *out,
            int n_ints,
            const char *sep)
{
        unsigned long v;
        char *tail;

        for (int i = 0; i < n_ints; i++) {
                while (isspace(**p))
                        (*p)++;

                errno = 0;
                v = strtoul(*p, &tail, 10);
                if (errno != 0 || tail == *p || v > UINT_MAX)
                        return false;
                *(out++) = (unsigned) v;
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
parse_int64s(const char **p,
             int64_t *out,
             int n_ints,
             const char *sep)
{
        long long v;
        char *tail;

        for (int i = 0; i < n_ints; i++) {
                while (isspace(**p))
                        (*p)++;

                errno = 0;
                v = strtoll(*p, &tail, 10);
                if (errno != 0 || tail == *p ||
                    v < INT64_MIN || v > INT64_MAX)
                        return false;
                *(out++) = (int64_t) v;
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
parse_uint64s(const char **p,
              uint64_t *out,
              int n_ints,
              const char *sep)
{
        unsigned long long v;
        char *tail;

        for (int i = 0; i < n_ints; i++) {
                while (isspace(**p))
                        (*p)++;

                errno = 0;
                v = strtoull(*p, &tail, 10);
                if (errno != 0 || tail == *p || v > UINT64_MAX)
                        return false;
                *(out++) = (uint64_t) v;
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
parse_mat(const char **p,
          float *out,
          enum vr_script_type type)
{
        int num_rows = type_rows(type);
        int num_cols = type_columns(type);
        int stride = type_matrix_stride(type);

        for (int col = 0; col < num_cols; col++) {
                if (!parse_floats(p, out, num_rows, NULL))
                        return false;
                out += stride / sizeof *out;
        }

        return true;
}

static bool
parse_dmat(const char **p,
           double *out,
           enum vr_script_type type)
{
        int num_rows = type_rows(type);
        int num_cols = type_columns(type);
        int stride = type_matrix_stride(type);

        for (int col = 0; col < num_cols; col++) {
                if (!parse_doubles(p, out, num_rows, NULL))
                        return false;
                out += stride / sizeof *out;
        }

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
                { "uint ", VR_SCRIPT_TYPE_UINT },
                { "int64_t ", VR_SCRIPT_TYPE_INT64 },
                { "uint64_t ", VR_SCRIPT_TYPE_UINT64 },
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
                { "uvec2 ", VR_SCRIPT_TYPE_UVEC2 },
                { "uvec3 ", VR_SCRIPT_TYPE_UVEC3 },
                { "uvec4 ", VR_SCRIPT_TYPE_UVEC4 },
                { "i64vec2 ", VR_SCRIPT_TYPE_I64VEC2 },
                { "i64vec3 ", VR_SCRIPT_TYPE_I64VEC3 },
                { "i64vec4 ", VR_SCRIPT_TYPE_I64VEC4 },
                { "u64vec2 ", VR_SCRIPT_TYPE_U64VEC2 },
                { "u64vec3 ", VR_SCRIPT_TYPE_U64VEC3 },
                { "u64vec4 ", VR_SCRIPT_TYPE_U64VEC4 },
                { "mat2 ", VR_SCRIPT_TYPE_MAT2 },
                { "mat2x2 ", VR_SCRIPT_TYPE_MAT2 },
                { "mat2x3 ", VR_SCRIPT_TYPE_MAT2X3 },
                { "mat2x4 ", VR_SCRIPT_TYPE_MAT2X4 },
                { "mat3x2 ", VR_SCRIPT_TYPE_MAT3X2 },
                { "mat3 ", VR_SCRIPT_TYPE_MAT3 },
                { "mat3x3 ", VR_SCRIPT_TYPE_MAT3 },
                { "mat3x4 ", VR_SCRIPT_TYPE_MAT3X4 },
                { "mat4x2 ", VR_SCRIPT_TYPE_MAT4X2 },
                { "mat4x3 ", VR_SCRIPT_TYPE_MAT4X3 },
                { "mat4 ", VR_SCRIPT_TYPE_MAT4 },
                { "mat4x4 ", VR_SCRIPT_TYPE_MAT4 },
                { "dmat2 ", VR_SCRIPT_TYPE_DMAT2 },
                { "dmat2x2 ", VR_SCRIPT_TYPE_DMAT2 },
                { "dmat2x3 ", VR_SCRIPT_TYPE_DMAT2X3 },
                { "dmat2x4 ", VR_SCRIPT_TYPE_DMAT2X4 },
                { "dmat3x2 ", VR_SCRIPT_TYPE_DMAT3X2 },
                { "dmat3 ", VR_SCRIPT_TYPE_DMAT3 },
                { "dmat3x3 ", VR_SCRIPT_TYPE_DMAT3 },
                { "dmat3x4 ", VR_SCRIPT_TYPE_DMAT3X4 },
                { "dmat4x2 ", VR_SCRIPT_TYPE_DMAT4X2 },
                { "dmat4x3 ", VR_SCRIPT_TYPE_DMAT4X3 },
                { "dmat4 ", VR_SCRIPT_TYPE_DMAT4 },
                { "dmat4x4 ", VR_SCRIPT_TYPE_DMAT4 },
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
        case VR_SCRIPT_TYPE_UINT:
                return parse_uints(p, &value->u, 1, NULL);
        case VR_SCRIPT_TYPE_INT64:
                return parse_int64s(p, &value->i64, 1, NULL);
        case VR_SCRIPT_TYPE_UINT64:
                return parse_uint64s(p, &value->u64, 1, NULL);
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
        case VR_SCRIPT_TYPE_UVEC2:
                return parse_uints(p, value->uvec, 2, NULL);
        case VR_SCRIPT_TYPE_UVEC3:
                return parse_uints(p, value->uvec, 3, NULL);
        case VR_SCRIPT_TYPE_UVEC4:
                return parse_uints(p, value->uvec, 4, NULL);
        case VR_SCRIPT_TYPE_I64VEC2:
                return parse_int64s(p, value->i64vec, 2, NULL);
        case VR_SCRIPT_TYPE_I64VEC3:
                return parse_int64s(p, value->i64vec, 3, NULL);
        case VR_SCRIPT_TYPE_I64VEC4:
                return parse_int64s(p, value->i64vec, 4, NULL);
        case VR_SCRIPT_TYPE_U64VEC2:
                return parse_uint64s(p, value->u64vec, 2, NULL);
        case VR_SCRIPT_TYPE_U64VEC3:
                return parse_uint64s(p, value->u64vec, 3, NULL);
        case VR_SCRIPT_TYPE_U64VEC4:
                return parse_uint64s(p, value->u64vec, 4, NULL);
        case VR_SCRIPT_TYPE_MAT2:
        case VR_SCRIPT_TYPE_MAT2X3:
        case VR_SCRIPT_TYPE_MAT2X4:
        case VR_SCRIPT_TYPE_MAT3X2:
        case VR_SCRIPT_TYPE_MAT3:
        case VR_SCRIPT_TYPE_MAT3X4:
        case VR_SCRIPT_TYPE_MAT4X2:
        case VR_SCRIPT_TYPE_MAT4X3:
        case VR_SCRIPT_TYPE_MAT4:
                return parse_mat(p, value->mat, value->type);
        case VR_SCRIPT_TYPE_DMAT2:
        case VR_SCRIPT_TYPE_DMAT2X3:
        case VR_SCRIPT_TYPE_DMAT2X4:
        case VR_SCRIPT_TYPE_DMAT3X2:
        case VR_SCRIPT_TYPE_DMAT3:
        case VR_SCRIPT_TYPE_DMAT3X4:
        case VR_SCRIPT_TYPE_DMAT4X2:
        case VR_SCRIPT_TYPE_DMAT4X3:
        case VR_SCRIPT_TYPE_DMAT4:
                return parse_dmat(p, value->dmat, value->type);
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

        int extension_len = 0;

        while (true) {
                char ch = start[extension_len];

                if ((ch < 'A' || ch > 'Z') &&
                    (ch < 'a' || ch > 'z') &&
                    (ch < '0' || ch > '9') &&
                    ch != '_')
                        break;

                extension_len++;
        }

        if (is_end(start + extension_len)) {
                int n_extensions =
                        data->extensions.length / sizeof (char *) - 1;
                vr_buffer_set_length(&data->extensions,
                                     (n_extensions + 2) * sizeof (char *));
                char **extensions = (char **) data->extensions.data;
                extensions[n_extensions++] = vr_strndup(start, extension_len);
                extensions[n_extensions++] = NULL;
                return true;
        }

        vr_error_message("%s:%i: Invalid require line",
                         data->filename,
                         data->line_num);

        return false;
}

static bool
process_draw_rect_command(struct load_state *data,
                          const char *p,
                          struct vr_script_command *command)
{
        if (!looking_at(&p, "draw rect "))
                return false;

        bool ortho = false;

        if (looking_at(&p, "ortho "))
                ortho = true;

        struct vr_pipeline_key *key = &command->draw_rect.key;

        *key = data->current_key;
        key->source = VR_PIPELINE_KEY_SOURCE_RECTANGLE;

        if (looking_at(&p, "patch "))
                key->topology.i = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        else
                key->topology.i = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        key->patchControlPoints.i = 4;

        if (!parse_floats(&p, &command->draw_rect.x, 4, NULL) ||
            !is_end(p))
                return false;

        command->op = VR_SCRIPT_OP_DRAW_RECT;

        if (ortho) {
                command->draw_rect.x = (command->draw_rect.x * 2.0f /
                                        VR_WINDOW_WIDTH) - 1.0f;
                command->draw_rect.y = (command->draw_rect.y * 2.0f /
                                        VR_WINDOW_HEIGHT) - 1.0f;
                command->draw_rect.w *= 2.0f / VR_WINDOW_WIDTH;
                command->draw_rect.h *= 2.0f / VR_WINDOW_HEIGHT;
        }

        return true;
}

static bool
process_probe_command(const char *p,
                      struct vr_script_command *command)
{
        bool relative = false;
        enum { POINT, RECT, ALL } region_type = POINT;
        int n_components;

        if (looking_at(&p, "relative "))
                relative = true;

        if (!looking_at(&p, "probe "))
                return false;

        if (looking_at(&p, "rect "))
                region_type = RECT;
        else if (looking_at(&p, "all "))
                region_type = ALL;

        if (looking_at(&p, "rgb "))
                n_components = 3;
        else if (looking_at(&p, "rgba "))
                n_components = 4;
        else
                return false;

        command->op = VR_SCRIPT_OP_PROBE_RECT;
        command->probe_rect.n_components = n_components;

        if (region_type == ALL) {
                if (relative)
                        return false;
                if (!parse_doubles(&p,
                                   command->probe_rect.color,
                                   n_components,
                                   NULL))
                        return false;
                if (!is_end(p))
                        return false;
                command->probe_rect.x = 0;
                command->probe_rect.y = 0;
                command->probe_rect.w = VR_WINDOW_WIDTH;
                command->probe_rect.h = VR_WINDOW_HEIGHT;
                return true;
        }

        while (isspace(*p))
                p++;
        if (*p != '(')
                return false;
        p++;

        if (region_type == POINT) {
                if (relative) {
                        float rel_pos[2];
                        if (!parse_floats(&p, rel_pos, 2, ","))
                                return false;
                        command->probe_rect.x = rel_pos[0] * VR_WINDOW_WIDTH;
                        command->probe_rect.y = rel_pos[1] * VR_WINDOW_HEIGHT;
                } else if (!parse_ints(&p, &command->probe_rect.x, 2, ",")) {
                        return false;
                }
                command->probe_rect.w = 1;
                command->probe_rect.h = 1;
        } else {
                assert(region_type == RECT);

                if (relative) {
                        float rel_pos[4];
                        if (!parse_floats(&p, rel_pos, 4, ","))
                                return false;
                        command->probe_rect.x = rel_pos[0] * VR_WINDOW_WIDTH;
                        command->probe_rect.y = rel_pos[1] * VR_WINDOW_HEIGHT;
                        command->probe_rect.w = rel_pos[2] * VR_WINDOW_WIDTH;
                        command->probe_rect.h = rel_pos[3] * VR_WINDOW_HEIGHT;
                } else if (!parse_ints(&p, &command->probe_rect.x, 4, ",")) {
                        return false;
                }
        }

        while (isspace(*p))
                p++;
        if (*p != ')')
                return false;
        p++;

        while (isspace(*p))
                p++;
        if (*p != '(')
                return false;
        p++;

        if (!parse_doubles(&p, command->probe_rect.color, n_components, ","))
                return false;

        while (isspace(*p))
                p++;
        if (*p != ')')
                return false;
        p++;

        if (!is_end(p))
                return false;

        return true;
}

static bool
process_draw_arrays_command(struct load_state *data,
                            const char *p,
                            struct vr_script_command *command)
{
        int args[3] = { [2] = 1 };
        int n_args = 2;

        command->draw_arrays.indexed = false;
        command->draw_arrays.key = data->current_key;
        command->draw_arrays.key.source = VR_PIPELINE_KEY_SOURCE_VERTEX_DATA;

        while (true) {
                if (looking_at(&p, "instanced ")) {
                        n_args = 3;
                        continue;
                } else if (looking_at(&p, "indexed ")) {
                        command->draw_arrays.indexed = true;
                        continue;
                }

                break;
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
                        command->draw_arrays.key.topology.i =
                                topologies[i].topology;
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
process_indices_line(struct load_state *data)
{
        const char *p = (char *) data->line.data;

        while (true) {
                while (*p && isspace(*p))
                        p++;

                if (*p == '\0' || *p == '#')
                        return true;

                vr_buffer_set_length(&data->indices,
                                     data->indices.length + sizeof (uint16_t));
                errno = 0;
                char *tail;
                unsigned value = strtoul(p, &tail, 10);

                if (errno || value > UINT16_MAX) {
                        vr_error_message("%s:%i: Invalid index",
                                         data->filename,
                                         data->line_num);
                        return false;
                }

                uint16_t *index = (uint16_t *) (data->indices.data +
                                                data->indices.length) - 1;
                *index = value;
                p = tail;
        }

        return true;
}

static bool
process_bool_property(struct load_state *data,
                      union vr_pipeline_key_value *value,
                      const char *p)
{
        if (looking_at(&p, "true"))
                value->i = true;
        else if (looking_at(&p, "false"))
                value->i = false;
        else if (!parse_ints(&p, &value->i, 1, NULL))
                goto error;

        if (!is_end(p))
                goto error;

        return true;

error:
        vr_error_message("%s:%i: Invalid boolean value",
                         data->filename,
                         data->line_num);
        return false;
}

static bool
process_int_property(struct load_state *data,
                     union vr_pipeline_key_value *value,
                     const char *p)
{
        value->i = 0;

        while (true) {
                int this_int;

                while (isspace(*p))
                        p++;

                if (parse_ints(&p, &this_int, 1, NULL)) {
                        value->i |= this_int;
                } else if (isalnum(*p)) {
                        const char *end = p + 1;
                        while (isalnum(*end) || *end == '_')
                                end++;
                        char *enum_name = vr_strndup(p, end - p);
                        bool is_enum = vr_pipeline_key_lookup_enum(enum_name,
                                                                   &this_int);
                        free(enum_name);

                        if (!is_enum)
                                goto error;

                        value->i |= this_int;
                        p = end;
                } else {
                        goto error;
                }

                if (is_end(p))
                        break;

                while (isspace(*p))
                        p++;

                if (*p != '|')
                        goto error;
                p++;
        }

        return true;

error:
        vr_error_message("%s:%i: Invalid int value",
                         data->filename,
                         data->line_num);
        return false;
}

static bool
process_float_property(struct load_state *data,
                       union vr_pipeline_key_value *value,
                       const char *p)
{
        while (isspace(*p))
                p++;

        if (!parse_floats(&p, &value->f, 1, NULL) || !is_end(p)) {
                vr_error_message("%s:%i: Invalid float value",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        return true;
}

static bool
process_pipeline_property(struct load_state *data,
                          union vr_pipeline_key_value *value,
                          enum vr_pipeline_key_value_type type,
                          const char *p)
{
        while (*p && isspace(*p))
                p++;

        switch (type) {
        case VR_PIPELINE_KEY_VALUE_TYPE_BOOL:
                return process_bool_property(data, value, p);
        case VR_PIPELINE_KEY_VALUE_TYPE_INT:
                return process_int_property(data, value, p);
        case VR_PIPELINE_KEY_VALUE_TYPE_FLOAT:
                return process_float_property(data, value, p);
        }

        vr_fatal("Unknown pipeline property type");
}

static bool
process_test_line(struct load_state *data)
{
        const char *p = (char *) data->line.data;

        while (*p && isspace(*p))
                p++;

        if (*p == '#' || *p == '\0')
                return true;

        if (looking_at(&p, "patch parameter vertices ")) {
                struct vr_pipeline_key *key = &data->current_key;
                if (!parse_ints(&p, &key->patchControlPoints.i, 1, NULL))
                        goto error;
                if (!is_end(p))
                        goto error;
                return true;
        }

        if (isalnum(*p)) {
                const char *end = p + 1;
                while (isalnum(*end))
                        end++;
                char *prop_name = vr_strndup(p, end - p);

                enum vr_pipeline_key_value_type key_value_type;
                union vr_pipeline_key_value *key_value =
                        vr_pipeline_key_lookup(&data->current_key,
                                               prop_name,
                                               &key_value_type);

                vr_free(prop_name);

                if (key_value) {
                        return process_pipeline_property(data,
                                                         key_value,
                                                         key_value_type,
                                                         end);
                }
        }

        vr_buffer_set_length(&data->commands,
                             data->commands.length +
                             sizeof (struct vr_script_command));
        struct vr_script_command *command =
                (struct vr_script_command *)
                (data->commands.data +
                 data->commands.length -
                 sizeof (struct vr_script_command));

        command->line_num = data->line_num;

        if (process_draw_rect_command(data, p, command))
                return true;

        if (process_probe_command(p, command))
                return true;

        if (looking_at(&p, "draw arrays "))
                return process_draw_arrays_command(data, p, command);

        if (looking_at(&p, "uniform ubo ")) {
                if (!parse_uints(&p, &command->set_ubo_uniform.ubo, 1, NULL))
                        goto error;
                if (command->set_ubo_uniform.ubo >= sizeof (unsigned) * 8) {
                        vr_error_message("%s:%i: UBO binding number is "
                                         "too large",
                                         data->filename,
                                         data->line_num);
                        return false;
                }
                while (isspace(*p))
                        p++;
                if (!parse_value_type(&p, &command->set_ubo_uniform.value.type))
                        goto error;
                if (!parse_size_t(&p, &command->set_ubo_uniform.offset))
                        goto error;
                if (!parse_value(&p, &command->set_ubo_uniform.value))
                        goto error;
                if (!is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_SET_UBO_UNIFORM;
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
start_spirv_shader(struct load_state *data,
                   enum vr_script_shader_stage stage)
{
        if (!vr_list_empty(&data->script->stages[stage])) {
                vr_error_message("%s:%i: SPIR-V source can not be "
                                 "linked with other shaders in the "
                                 "same stage",
                                 data->filename,
                                 data->line_num);
                return false;
        }

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
                if (data->current_source_type == VR_SCRIPT_SOURCE_TYPE_SPIRV &&
                    !start_spirv_shader(data, data->current_stage))
                        return false;

                return true;
        }

        if (is_string("vertex shader passthrough", start, end)) {
                if (!start_spirv_shader(data, VR_SCRIPT_SHADER_STAGE_VERTEX))
                        return false;
                data->current_section = SECTION_NONE;
                add_shader(data,
                           VR_SCRIPT_SHADER_STAGE_VERTEX,
                           VR_SCRIPT_SOURCE_TYPE_SPIRV,
                           (sizeof vertex_shader_passthrough) - 1,
                           vertex_shader_passthrough);
                return true;
        }

        if (is_string("comment", start, end)) {
                data->current_section = SECTION_COMMENT;
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

        if (is_string("indices", start, end)) {
                data->current_section = SECTION_INDICES;
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

        case SECTION_COMMENT:
                return true;

        case SECTION_REQUIRE:
                return process_require_line(data);

        case SECTION_SHADER:
        case SECTION_VERTEX_DATA:
                vr_buffer_append(&data->buffer,
                                 data->line.data,
                                 data->line.length);
                return true;

        case SECTION_INDICES:
                return process_indices_line(data);

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
                .extensions = VR_BUFFER_STATIC_INIT,
                .current_stage = -1,
                .current_section = SECTION_NONE,
        };
        bool res = true;
        int stage;

        vr_pipeline_key_init(&data.current_key);

        data.script->filename = vr_strdup(filename);
        data.script->framebuffer_format =
                vr_format_lookup_by_vk_format(VK_FORMAT_B8G8R8A8_UNORM);
        assert(data.script->framebuffer_format != NULL);

        vr_buffer_set_length(&data.extensions, sizeof (const char *));
        memset(data.extensions.data, 0, data.extensions.length);

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

        data.script->commands = (struct vr_script_command *) data.commands.data;
        data.script->n_commands = (data.commands.length /
                                   sizeof (struct vr_script_command));
        data.script->extensions =
                (const char *const *) data.extensions.data;
        data.script->indices = (uint16_t *) data.indices.data;
        data.script->n_indices =
                data.indices.length / sizeof (uint16_t);

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

        vr_free(script->indices);

        vr_free(script->filename);

        vr_free(script->commands);

        if (script->extensions != NULL) {
                for (const char *const *ext = script->extensions; *ext; ext++)
                        vr_free((char *) *ext);

                vr_free((void *) script->extensions);
        }

        vr_free(script);
}

size_t
vr_script_type_size(enum vr_script_type type)
{
        switch (type) {
        case VR_SCRIPT_TYPE_INT:
        case VR_SCRIPT_TYPE_UINT:
        case VR_SCRIPT_TYPE_FLOAT:
                return 4;
        case VR_SCRIPT_TYPE_INT64:
        case VR_SCRIPT_TYPE_UINT64:
        case VR_SCRIPT_TYPE_DOUBLE:
                return 8;
        case VR_SCRIPT_TYPE_VEC2:
        case VR_SCRIPT_TYPE_IVEC2:
        case VR_SCRIPT_TYPE_UVEC2:
                return 4 * 2;
        case VR_SCRIPT_TYPE_VEC3:
        case VR_SCRIPT_TYPE_IVEC3:
        case VR_SCRIPT_TYPE_UVEC3:
                return 4 * 3;
        case VR_SCRIPT_TYPE_VEC4:
        case VR_SCRIPT_TYPE_IVEC4:
        case VR_SCRIPT_TYPE_UVEC4:
                return 4 * 4;
        case VR_SCRIPT_TYPE_DVEC2:
        case VR_SCRIPT_TYPE_I64VEC2:
        case VR_SCRIPT_TYPE_U64VEC2:
                return 8 * 2;
        case VR_SCRIPT_TYPE_DVEC3:
        case VR_SCRIPT_TYPE_I64VEC3:
        case VR_SCRIPT_TYPE_U64VEC3:
                return 8 * 3;
        case VR_SCRIPT_TYPE_DVEC4:
        case VR_SCRIPT_TYPE_I64VEC4:
        case VR_SCRIPT_TYPE_U64VEC4:
                return 8 * 4;
        case VR_SCRIPT_TYPE_MAT2:
        case VR_SCRIPT_TYPE_MAT2X3:
        case VR_SCRIPT_TYPE_MAT2X4:
        case VR_SCRIPT_TYPE_MAT3X2:
        case VR_SCRIPT_TYPE_MAT3:
        case VR_SCRIPT_TYPE_MAT3X4:
        case VR_SCRIPT_TYPE_MAT4X2:
        case VR_SCRIPT_TYPE_MAT4X3:
        case VR_SCRIPT_TYPE_MAT4:
        case VR_SCRIPT_TYPE_DMAT2:
        case VR_SCRIPT_TYPE_DMAT2X3:
        case VR_SCRIPT_TYPE_DMAT2X4:
        case VR_SCRIPT_TYPE_DMAT3X2:
        case VR_SCRIPT_TYPE_DMAT3:
        case VR_SCRIPT_TYPE_DMAT3X4:
        case VR_SCRIPT_TYPE_DMAT4X2:
        case VR_SCRIPT_TYPE_DMAT4X3:
        case VR_SCRIPT_TYPE_DMAT4:
                return type_matrix_stride(type) * type_columns(type);
        }

        vr_fatal("should not be reached");
}
