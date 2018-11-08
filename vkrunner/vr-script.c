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
#include <stdlib.h>

#include "vr-script-private.h"
#include "vr-list.h"
#include "vr-util.h"
#include "vr-buffer.h"
#include "vr-error-message.h"
#include "vr-feature-offsets.h"
#include "vr-window.h"
#include "vr-format-private.h"
#include "vr-source-private.h"
#include "vr-tolerance.h"
#include "vr-stream.h"

#define DEFAULT_TOLERANCE 0.01

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
        const struct vr_config *config;
        const struct vr_source *source;
        const char *filename;
        int line_num;
        struct vr_script *script;
        struct vr_buffer buffer;
        struct vr_buffer line;
        enum vr_shader_stage current_stage;
        enum vr_script_source_type current_source_type;
        enum section current_section;
        struct vr_buffer commands;
        struct vr_buffer pipeline_keys;
        struct vr_buffer extensions;
        struct vr_buffer buffers;
        struct vr_pipeline_key current_key;
        struct vr_buffer indices;
        float clear_color[4];
        float clear_depth;
        unsigned clear_stencil;
        struct vr_tolerance tolerance;
        int had_sections;
};

static const char *
stage_names[VR_SHADER_STAGE_N_STAGES] = {
        "vertex",
        "tessellation control",
        "tessellation evaluation",
        "geometry",
        "fragment",
        "compute",
};

static uint32_t
vertex_shader_passthrough[] = {
        0x07230203, 0x00010000, 0x00070000, 0x0000000c,
        0x00000000, 0x00020011, 0x00000001, 0x0003000e,
        0x00000000, 0x00000001, 0x0007000f, 0x00000000,
        0x00000001, 0x6e69616d, 0x00000000, 0x00000002,
        0x00000003, 0x00040047, 0x00000002, 0x0000001e,
        0x00000000, 0x00040047, 0x00000003, 0x0000000b,
        0x00000000, 0x00020013, 0x00000004, 0x00030021,
        0x00000005, 0x00000004, 0x00030016, 0x00000006,
        0x00000020, 0x00040017, 0x00000007, 0x00000006,
        0x00000004, 0x00040020, 0x00000008, 0x00000001,
        0x00000007, 0x00040020, 0x00000009, 0x00000003,
        0x00000007, 0x0004003b, 0x00000008, 0x00000002,
        0x00000001, 0x0004003b, 0x00000009, 0x00000003,
        0x00000003, 0x00050036, 0x00000004, 0x00000001,
        0x00000000, 0x00000005, 0x000200f8, 0x0000000a,
        0x0004003d, 0x00000007, 0x0000000b, 0x00000002,
        0x0003003e, 0x00000003, 0x0000000b, 0x000100fd,
        0x00010038
};

static void
add_shader(struct vr_script *script,
           enum vr_shader_stage stage,
           enum vr_script_source_type source_type,
           size_t length,
           const char *source)
{
        struct vr_script_shader *shader;

        shader = vr_alloc(sizeof *shader + length);
        shader->length = length;
        shader->source_type = source_type;
        memcpy(shader->source, source, length);

        vr_list_insert(script->stages[stage].prev, &shader->link);
}

static bool
end_shader(struct load_state *data)
{
        add_shader(data->script,
                   data->current_stage,
                   data->current_source_type,
                   data->buffer.length,
                   (const char *) data->buffer.data);

        return true;
}

static bool
end_vertex_data(struct load_state *data)
{
        data->script->vertex_data = vr_vbo_parse(data->config,
                                                 (const char *)
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
                return end_shader(data);

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

static bool
parse_floats(struct load_state *data,
             const char **p,
             float *out,
             int n_floats,
             const char *sep)
{
        char *tail;

        for (int i = 0; i < n_floats; i++) {
                while (isspace(**p))
                        (*p)++;

                errno = 0;
                *(out++) = vr_strtof(&data->config->strtof_data, *p, &tail);
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
parse_doubles(struct load_state *data,
              const char **p,
              double *out,
              int n_doubles,
              const char *sep)
{
        char *tail;

        for (int i = 0; i < n_doubles; i++) {
                while (isspace(**p))
                        (*p)++;

                errno = 0;
                *(out++) = vr_strtod(&data->config->strtof_data, *p, &tail);
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
parse_int8s(const char **p,
            int8_t *out,
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
                    v < INT8_MIN || v > INT8_MAX)
                        return false;
                *(out++) = (int8_t) v;
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
parse_uint8s(const char **p,
             uint8_t *out,
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
                if (errno != 0 || tail == *p || v > UINT8_MAX)
                        return false;
                *(out++) = (uint8_t) v;
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
parse_int16s(const char **p,
             int16_t *out,
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
                    v < INT16_MIN || v > INT16_MAX)
                        return false;
                *(out++) = (int16_t) v;
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
parse_uint16s(const char **p,
              uint16_t *out,
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
                if (errno != 0 || tail == *p || v > UINT16_MAX)
                        return false;
                *(out++) = (uint16_t) v;
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
parse_desc_set_and_binding(const char **p,
                           unsigned *out)
{
        const char *p_backup = *p;
        if (!parse_uints(p, out, 2, ":")) {
                *p = p_backup;
                out[0] = 0;
                if (!parse_uints(p, &out[1], 1, NULL))
                        return false;
        }
        return true;
}

static bool
parse_value_type(const char **p,
                 enum vr_box_type *type)
{
        static const struct {
                const char *name;
                enum vr_box_type type;
        } types[] = {
                { "int ", VR_BOX_TYPE_INT },
                { "uint ", VR_BOX_TYPE_UINT },
                { "int8_t ", VR_BOX_TYPE_INT8 },
                { "uint8_t ", VR_BOX_TYPE_UINT8 },
                { "int16_t ", VR_BOX_TYPE_INT16 },
                { "uint16_t ", VR_BOX_TYPE_UINT16 },
                { "int64_t ", VR_BOX_TYPE_INT64 },
                { "uint64_t ", VR_BOX_TYPE_UINT64 },
                { "float ", VR_BOX_TYPE_FLOAT },
                { "double ", VR_BOX_TYPE_DOUBLE },
                { "vec2 ", VR_BOX_TYPE_VEC2 },
                { "vec3 ", VR_BOX_TYPE_VEC3 },
                { "vec4 ", VR_BOX_TYPE_VEC4 },
                { "dvec2 ", VR_BOX_TYPE_DVEC2 },
                { "dvec3 ", VR_BOX_TYPE_DVEC3 },
                { "dvec4 ", VR_BOX_TYPE_DVEC4 },
                { "ivec2 ", VR_BOX_TYPE_IVEC2 },
                { "ivec3 ", VR_BOX_TYPE_IVEC3 },
                { "ivec4 ", VR_BOX_TYPE_IVEC4 },
                { "uvec2 ", VR_BOX_TYPE_UVEC2 },
                { "uvec3 ", VR_BOX_TYPE_UVEC3 },
                { "uvec4 ", VR_BOX_TYPE_UVEC4 },
                { "i8vec2 ", VR_BOX_TYPE_I8VEC2 },
                { "i8vec3 ", VR_BOX_TYPE_I8VEC3 },
                { "i8vec4 ", VR_BOX_TYPE_I8VEC4 },
                { "u8vec2 ", VR_BOX_TYPE_U8VEC2 },
                { "u8vec3 ", VR_BOX_TYPE_U8VEC3 },
                { "u8vec4 ", VR_BOX_TYPE_U8VEC4 },
                { "i16vec2 ", VR_BOX_TYPE_I16VEC2 },
                { "i16vec3 ", VR_BOX_TYPE_I16VEC3 },
                { "i16vec4 ", VR_BOX_TYPE_I16VEC4 },
                { "u16vec2 ", VR_BOX_TYPE_U16VEC2 },
                { "u16vec3 ", VR_BOX_TYPE_U16VEC3 },
                { "u16vec4 ", VR_BOX_TYPE_U16VEC4 },
                { "i64vec2 ", VR_BOX_TYPE_I64VEC2 },
                { "i64vec3 ", VR_BOX_TYPE_I64VEC3 },
                { "i64vec4 ", VR_BOX_TYPE_I64VEC4 },
                { "u64vec2 ", VR_BOX_TYPE_U64VEC2 },
                { "u64vec3 ", VR_BOX_TYPE_U64VEC3 },
                { "u64vec4 ", VR_BOX_TYPE_U64VEC4 },
                { "mat2 ", VR_BOX_TYPE_MAT2 },
                { "mat2x2 ", VR_BOX_TYPE_MAT2 },
                { "mat2x3 ", VR_BOX_TYPE_MAT2X3 },
                { "mat2x4 ", VR_BOX_TYPE_MAT2X4 },
                { "mat3x2 ", VR_BOX_TYPE_MAT3X2 },
                { "mat3 ", VR_BOX_TYPE_MAT3 },
                { "mat3x3 ", VR_BOX_TYPE_MAT3 },
                { "mat3x4 ", VR_BOX_TYPE_MAT3X4 },
                { "mat4x2 ", VR_BOX_TYPE_MAT4X2 },
                { "mat4x3 ", VR_BOX_TYPE_MAT4X3 },
                { "mat4 ", VR_BOX_TYPE_MAT4 },
                { "mat4x4 ", VR_BOX_TYPE_MAT4 },
                { "dmat2 ", VR_BOX_TYPE_DMAT2 },
                { "dmat2x2 ", VR_BOX_TYPE_DMAT2 },
                { "dmat2x3 ", VR_BOX_TYPE_DMAT2X3 },
                { "dmat2x4 ", VR_BOX_TYPE_DMAT2X4 },
                { "dmat3x2 ", VR_BOX_TYPE_DMAT3X2 },
                { "dmat3 ", VR_BOX_TYPE_DMAT3 },
                { "dmat3x3 ", VR_BOX_TYPE_DMAT3 },
                { "dmat3x4 ", VR_BOX_TYPE_DMAT3X4 },
                { "dmat4x2 ", VR_BOX_TYPE_DMAT4X2 },
                { "dmat4x3 ", VR_BOX_TYPE_DMAT4X3 },
                { "dmat4 ", VR_BOX_TYPE_DMAT4 },
                { "dmat4x4 ", VR_BOX_TYPE_DMAT4 },
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
parse_value(struct load_state *data,
            const char **p,
            enum vr_box_type type,
            void *value)
{
        const struct vr_box_type_info *info = vr_box_type_get_info(type);
        size_t stride = (vr_box_type_matrix_stride(type) /
                         vr_box_base_type_size(info->base_type));

        for (int col = 0; col < info->columns; col++) {
                switch (info->base_type) {
                case VR_BOX_BASE_TYPE_INT:
                        if (!parse_ints(p,
                                        (int *) value + col * stride,
                                        info->rows,
                                        NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_UINT:
                        if (!parse_uints(p,
                                         (unsigned *) value + col * stride,
                                         info->rows,
                                         NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_INT8:
                        if (!parse_int8s(p,
                                         (int8_t *) value + col * stride,
                                         info->rows,
                                         NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_UINT8:
                        if (!parse_uint8s(p,
                                          (uint8_t *) value + col * stride,
                                          info->rows,
                                          NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_INT16:
                        if (!parse_int16s(p,
                                          (int16_t *) value + col * stride,
                                          info->rows,
                                          NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_UINT16:
                        if (!parse_uint16s(p,
                                           (uint16_t *) value + col * stride,
                                           info->rows,
                                           NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_INT64:
                        if (!parse_int64s(p,
                                          (int64_t *) value + col * stride,
                                          info->rows,
                                          NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_UINT64:
                        if (!parse_uint64s(p,
                                           (uint64_t *) value + col * stride,
                                           info->rows,
                                           NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_FLOAT:
                        if (!parse_floats(data,
                                          p,
                                          (float *) value + col * stride,
                                          info->rows,
                                          NULL))
                                return false;
                        break;
                case VR_BOX_BASE_TYPE_DOUBLE:
                        if (!parse_doubles(data,
                                           p,
                                           (double *) value + col * stride,
                                           info->rows,
                                           NULL))
                                return false;
                        break;
                }
        }

        return true;
}

static bool
parse_box_values(struct load_state *data,
                 const char **p,
                 enum vr_box_type type,
                 size_t alignment,
                 size_t *size_out,
                 void **buffer_out)
{
        struct vr_buffer buffer = VR_BUFFER_STATIC_INIT;
        size_t type_size = vr_box_type_size(type);

        do {
                vr_buffer_set_length(&buffer,
                                     vr_align(buffer.length, alignment) +
                                     type_size);

                if (!parse_value(data,
                                 p,
                                 type,
                                 buffer.data + buffer.length - type_size)) {
                        vr_buffer_destroy(&buffer);
                        return false;
                }
        } while (!is_end(*p));

        *buffer_out = buffer.data;
        *size_out = buffer.length;

        return true;
}

static bool
parse_buffer_subdata(struct load_state *data,
                     const char **p,
                     enum vr_box_type type,
                     size_t *size_out,
                     void **buffer_out)
{
        size_t alignment = vr_box_type_base_alignment(type);

        return parse_box_values(data,
                                p,
                                type,
                                alignment,
                                size_out,
                                buffer_out);
}

static bool
parse_format(struct load_state *data,
             const char *p,
             const struct vr_format **format_out)
{
        while (isspace(*p))
                p++;
        const char *end = p;
        while (*end && !isspace(*end))
                end++;

        if (!is_end(end)) {
                vr_error_message(data->config,
                                 "%s:%i: Missing format name",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        char *format_name = vr_strndup(p, end - p);
        const struct vr_format *format = vr_format_lookup_by_name(format_name);
        bool ret;

        if (format == NULL) {
                vr_error_message(data->config,
                                 "%s:%i: Unknown format: %s",
                                 data->filename,
                                 data->line_num,
                                 format_name);
                ret = false;
        } else {
                *format_out = format;
                ret = true;
        }

        vr_free(format_name);

        return ret;
}

static bool
parse_fbsize(struct load_state *data,
             const char *p,
             struct vr_window_format *format)
{
        unsigned parts[2];

        if (!parse_uints(&p, parts, 2, NULL) ||
            parts[0] == 0 || parts[1] == 0 ||
            !is_end(p)) {
                vr_error_message(data->config,
                                 "%s:%i: Invalid fbsize",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        format->width = parts[0];
        format->height = parts[1];

        return true;
}

static bool
parse_tolerance(struct load_state *data,
                const char *p,
                int n_tolerance,
                bool parse_percent)
{
        if (parse_doubles(data,
                          &p,
                          data->tolerance.value,
                          n_tolerance,
                          parse_percent ? "%" : NULL)) {
                data->tolerance.is_percent = parse_percent;
                for (unsigned t = 0; t < n_tolerance; ++t) {
                        if (data->tolerance.value[t] < 0.0) {
                                vr_error_message(data->config,
                                                 "%s:%i: tolerance values "
                                                 "must be non-negative",
                                                 data->filename,
                                                 data->line_num);
                                return false;
                        }
                }

                if (n_tolerance == 1) {
                        data->tolerance.is_percent = looking_at(&p, "%");
                } else if (parse_percent && !looking_at(&p, "%")) {
                        vr_error_message(data->config,
                                         "%s:%i: only the last tolerance "
                                         "value is not a percent",
                                         data->filename,
                                         data->line_num);
                        return false;
                }

                if (!is_end(p)) {
                        vr_error_message(data->config,
                                         "%s:%i: tolerance command has extra "
                                         "arguments \"%s\"",
                                         data->filename,
                                         data->line_num,
                                         p);
                        return false;
                }

                for (unsigned t = n_tolerance; t < 4; ++t) {
                        data->tolerance.value[t] = data->tolerance.value[0];
                }
                return true;
        }
        return false;
}

static bool
process_none_line(struct load_state *data)
{
        const char *start = (char *) data->line.data;

        while (*start && isspace(*start))
                start++;

        if (*start != '#' && *start != '\0') {
                vr_error_message(data->config,
                                 "%s:%i expected empty line",
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
                struct vr_window_format *format =
                        &data->script->window_format;
                return parse_format(data, p, &format->color_format);
        }

        if (looking_at(&p, "depthstencil ")) {
                struct vr_window_format *format =
                        &data->script->window_format;
                return parse_format(data, p, &format->depth_stencil_format);
        }

        if (looking_at(&p, "fbsize "))
                return parse_fbsize(data, p, &data->script->window_format);

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

        vr_error_message(data->config,
                         "%s:%i: Invalid require line",
                         data->filename,
                         data->line_num);

        return false;
}

static unsigned
add_pipeline_key(struct load_state *data,
                 const struct vr_pipeline_key *key)
{
        unsigned n_keys = (data->pipeline_keys.length /
                           sizeof (struct vr_pipeline_key));
        const struct vr_pipeline_key *keys =
                (const struct vr_pipeline_key *) data->pipeline_keys.data;

        for (unsigned i = 0; i < n_keys; i++) {
                if (vr_pipeline_key_equal(keys + i, key))
                        return i;
        }

        vr_buffer_set_length(&data->pipeline_keys,
                             data->pipeline_keys.length + sizeof *key);
        vr_pipeline_key_copy((struct vr_pipeline_key *)
                             data->pipeline_keys.data +
                             n_keys,
                             key);

        return n_keys;
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

        struct vr_pipeline_key key;
        vr_pipeline_key_copy(&key, &data->current_key);
        key.type = VR_PIPELINE_KEY_TYPE_GRAPHICS;
        key.source = VR_PIPELINE_KEY_SOURCE_RECTANGLE;

        if (looking_at(&p, "patch "))
                key.topology.i = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        else
                key.topology.i = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        key.patchControlPoints.i = 4;

        command->draw_rect.pipeline_key = add_pipeline_key(data, &key);

        vr_pipeline_key_destroy(&key);

        if (!parse_floats(data, &p, &command->draw_rect.x, 4, NULL) ||
            !is_end(p))
                return false;

        command->op = VR_SCRIPT_OP_DRAW_RECT;

        if (ortho) {
                float width = data->script->window_format.width;
                float height = data->script->window_format.height;
                command->draw_rect.x = (command->draw_rect.x * 2.0f /
                                        width) - 1.0f;
                command->draw_rect.y = (command->draw_rect.y * 2.0f /
                                        height) - 1.0f;
                command->draw_rect.w *= 2.0f / width;
                command->draw_rect.h *= 2.0f / height;
        }

        return true;
}

static bool
process_probe_command(struct load_state *data,
                      const char *p,
                      struct vr_script_command *command)
{
        bool relative = false;
        enum { POINT, RECT, ALL } region_type = POINT;
        int n_components;

        command->probe_rect.tolerance = data->tolerance;

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

        size_t window_width = data->script->window_format.width;
        size_t window_height = data->script->window_format.height;

        if (region_type == ALL) {
                if (relative)
                        return false;
                if (!parse_doubles(data,
                                   &p,
                                   command->probe_rect.color,
                                   n_components,
                                   NULL))
                        return false;
                if (!is_end(p))
                        return false;
                command->probe_rect.x = 0;
                command->probe_rect.y = 0;
                command->probe_rect.w = window_width;
                command->probe_rect.h = window_height;
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
                        if (!parse_floats(data, &p, rel_pos, 2, ","))
                                return false;
                        command->probe_rect.x = rel_pos[0] * window_width;
                        command->probe_rect.y = rel_pos[1] * window_height;
                } else if (!parse_ints(&p, &command->probe_rect.x, 2, ",")) {
                        return false;
                }
                command->probe_rect.w = 1;
                command->probe_rect.h = 1;
        } else {
                assert(region_type == RECT);

                if (relative) {
                        float rel_pos[4];
                        if (!parse_floats(data, &p, rel_pos, 4, ","))
                                return false;
                        command->probe_rect.x = rel_pos[0] * window_width;
                        command->probe_rect.y = rel_pos[1] * window_height;
                        command->probe_rect.w = rel_pos[2] * window_width;
                        command->probe_rect.h = rel_pos[3] * window_height;
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

        if (!parse_doubles(data,
                           &p,
                           command->probe_rect.color,
                           n_components,
                           ","))
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
process_probe_ssbo_command(struct load_state *data,
                           const char *p,
                           struct vr_script_command *command)
{
        if (!looking_at(&p, "probe ssbo "))
                return false;

        if (!parse_value_type(&p, &command->probe_ssbo.type))
                return false;

        while (isspace(*p))
                p++;

        unsigned values[3];
        if (!parse_desc_set_and_binding(&p, values) ||
            !parse_uints(&p, &values[2], 1, NULL)) {
                return false;
        }

        command->probe_ssbo.desc_set = values[0];
        command->probe_ssbo.binding = values[1];
        command->probe_ssbo.offset = values[2];

        while (isspace(*p))
                p++;

        static const char *comparison_names[] =
        {
                [VR_BOX_COMPARISON_EQUAL] = "==",
                [VR_BOX_COMPARISON_FUZZY_EQUAL] = "~=",
                [VR_BOX_COMPARISON_NOT_EQUAL] = "!=",
                [VR_BOX_COMPARISON_LESS] = "<",
                [VR_BOX_COMPARISON_GREATER_EQUAL] = ">=",
                [VR_BOX_COMPARISON_GREATER] = ">",
                [VR_BOX_COMPARISON_LESS_EQUAL] = "<=",
        };

        for (unsigned i = 0; i < VR_N_ELEMENTS(comparison_names); i++) {
                if (looking_at(&p, comparison_names[i])) {
                        command->probe_ssbo.comparison = i;
                        goto found_comparison;
                }
        }
        return false;
found_comparison:

        while (isspace(*p))
                p++;

        size_t value_size;

        if (!parse_box_values(data,
                              &p,
                              command->probe_ssbo.type,
                              1, /* alignment */
                              &value_size,
                              &command->probe_ssbo.value))
                return false;

        if (!is_end(p)) {
                vr_free(command->probe_ssbo.value);
                return false;
        }

        size_t type_size = vr_box_type_size(command->probe_ssbo.type);
        command->probe_ssbo.n_values = value_size / type_size;
        command->op = VR_SCRIPT_OP_PROBE_SSBO;
        command->probe_ssbo.tolerance = data->tolerance;

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

        VkPrimitiveTopology topology;

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
                        topology = topologies[i].topology;
                        goto found_topology;
                }
        }

        vr_error_message(data->config,
                         "%s:%i: Unknown topology in draw arrays command",
                         data->filename,
                         data->line_num);
        return false;

found_topology:
        if (!parse_ints(&p, args, n_args, NULL) ||
            !is_end(p)) {
                vr_error_message(data->config,
                                 "%s:%i: Invalid draw arrays command",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        struct vr_pipeline_key key;
        vr_pipeline_key_copy(&key, &data->current_key);
        key.type = VR_PIPELINE_KEY_TYPE_GRAPHICS;
        key.source = VR_PIPELINE_KEY_SOURCE_VERTEX_DATA;
        key.topology.i = topology;

        command->op = VR_SCRIPT_OP_DRAW_ARRAYS;
        command->draw_arrays.first_vertex = args[0];
        command->draw_arrays.vertex_count = args[1];
        command->draw_arrays.first_instance = 0;
        command->draw_arrays.instance_count = args[2];
        command->draw_arrays.pipeline_key = add_pipeline_key(data, &key);

        vr_pipeline_key_destroy(&key);

        return true;
}

static bool
process_compute_command(struct load_state *data,
                        const char *p,
                        struct vr_script_command *command)
{
        if (!parse_uints(&p, &command->dispatch_compute.x, 3, NULL) ||
            !is_end(p)) {
                vr_error_message(data->config,
                                 "%s:%i: Invalid compute command",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        data->current_key.type = VR_PIPELINE_KEY_TYPE_COMPUTE;
        command->op = VR_SCRIPT_OP_DISPATCH_COMPUTE;
        command->dispatch_compute.pipeline_key =
                add_pipeline_key(data, &data->current_key);

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
                        vr_error_message(data->config,
                                         "%s:%i: Invalid index",
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
        vr_error_message(data->config,
                         "%s:%i: Invalid boolean value",
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
        vr_error_message(data->config,
                         "%s:%i: Invalid int value",
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

        if (!parse_floats(data, &p, &value->f, 1, NULL) || !is_end(p)) {
                vr_error_message(data->config,
                                 "%s:%i: Invalid float value",
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

static struct vr_script_buffer *
get_buffer(struct load_state *data,
           unsigned desc_set,
           unsigned binding,
           enum vr_script_buffer_type type)
{
        struct vr_script_buffer *buffer =
                (struct vr_script_buffer *) data->buffers.data;
        unsigned n_buffers = (data->buffers.length /
                              sizeof (struct vr_script_buffer));

        for (unsigned i = 0; i < n_buffers; i++) {
                if (buffer[i].desc_set == desc_set &&
                    buffer[i].binding == binding) {
                        if (buffer[i].type != type) {
                                vr_error_message(data->config,
                                                 "%s:%i: Buffer binding point "
                                                 "%u:%u used with different "
                                                 "types",
                                                 data->filename,
                                                 data->line_num,
                                                 desc_set,
                                                 binding);
                                return NULL;
                        }

                        return buffer + i;
                }
        }

        vr_buffer_set_length(&data->buffers,
                             data->buffers.length + sizeof *buffer);
        buffer = ((struct vr_script_buffer *)
                  (data->buffers.data + data->buffers.length) - 1);
        buffer->type = type;
        buffer->size = 0;
        buffer->desc_set = desc_set;
        buffer->binding = binding;

        return buffer;
}

static bool
process_set_buffer_subdata(struct load_state *data,
                           unsigned desc_set,
                           unsigned binding,
                           enum vr_script_buffer_type type,
                           const char *p,
                           struct vr_script_command *command)
{
        command->set_buffer_subdata.desc_set = desc_set;
        command->set_buffer_subdata.binding = binding;

        struct vr_script_buffer *buffer =
                get_buffer(data, desc_set, binding, type);
        if (buffer == NULL)
                return false;

        while (isspace(*p))
                p++;
        enum vr_box_type value_type;
        if (!parse_value_type(&p, &value_type))
                goto error;
        if (!parse_size_t(&p, &command->set_buffer_subdata.offset))
                goto error;
        if (!parse_buffer_subdata(data,
                                  &p,
                                  value_type,
                                  &command->set_buffer_subdata.size,
                                  &command->set_buffer_subdata.data))
                goto error;
        if (!is_end(p))
                goto error;

        size_t end = (command->set_buffer_subdata.offset +
                      command->set_buffer_subdata.size);
        if (end > buffer->size)
                buffer->size = end;

        command->op = VR_SCRIPT_OP_SET_BUFFER_SUBDATA;

        return true;

error:
        vr_error_message(data->config,
                         "%s:%i: Invalid set buffer subdata command",
                         data->filename,
                         data->line_num);
        return false;
}

static bool
process_set_ssbo_size(struct load_state *data,
                      unsigned desc_set,
                      unsigned binding,
                      unsigned size)
{
        struct vr_script_buffer *buffer =
                get_buffer(data, desc_set, binding, VR_SCRIPT_BUFFER_TYPE_SSBO);
        if (buffer == NULL)
                return false;

        if (size > buffer->size)
                buffer->size = size;

        return true;
}

static bool
process_entrypoint(struct load_state *data,
                   const char *p)
{
        int stage;

        for (stage = 0; stage < VR_SHADER_STAGE_N_STAGES; stage++) {
                if (looking_at(&p, stage_names[stage]))
                        goto found_stage;
        }

        return false;

found_stage:

        if (!looking_at(&p, " entrypoint "))
                return false;

        while (*p && isspace(*p))
                p++;

        const char *end = p + strlen(p);

        while (end > p && isspace(end[-1]))
                end--;

        if (end <= p)
                return false;

        char *entrypoint = vr_strndup(p, end - p);
        vr_pipeline_key_set_entrypoint(&data->current_key, stage, entrypoint);
        vr_free(entrypoint);

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

        const char *command_start = p;

        if (looking_at(&p, "patch parameter vertices ")) {
                struct vr_pipeline_key *key = &data->current_key;
                if (!parse_ints(&p, &key->patchControlPoints.i, 1, NULL))
                        goto error;
                if (!is_end(p))
                        goto error;
                return true;
        }

        if (looking_at(&p, "clear color ")) {
                if (!parse_floats(data, &p, data->clear_color, 4, NULL))
                        goto error;
                if (!is_end(p))
                        goto error;
                return true;
        }

        if (looking_at(&p, "clear depth ")) {
                if (!parse_floats(data, &p, &data->clear_depth, 1, NULL))
                        goto error;
                if (!is_end(p))
                        goto error;
                return true;
        }

        if (looking_at(&p, "clear stencil ")) {
                if (!parse_uints(&p, &data->clear_stencil, 1, NULL))
                        goto error;
                if (!is_end(p))
                        goto error;
                return true;
        }

        if (looking_at(&p, "ssbo ")) {
                unsigned values[3];
                if (parse_desc_set_and_binding(&p, values) &&
                    parse_uints(&p, &values[2], 1, NULL)) {
                        if (!is_end(p))
                                return false;
                        return process_set_ssbo_size(data,
                                                     values[0],
                                                     values[1],
                                                     values[2]);
                }
                p = command_start;
        }

        if (looking_at(&p, "tolerance ")) {
                if (parse_tolerance(data, p, 4, false))
                        return true;
                if (parse_tolerance(data, p, 4, true))
                        return true;
                if (parse_tolerance(data, p, 1, false))
                        return true;
                goto error;
        }

        if (process_entrypoint(data, p))
                return true;

        if (isalnum(*p)) {
                const char *end = p + 1;
                while (isalnum(*end) || *end == '.')
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

        memset(command, 0, sizeof *command);

        command->line_num = data->line_num;

        if (process_draw_rect_command(data, p, command))
                return true;

        if (process_probe_command(data, p, command))
                return true;

        if (process_probe_ssbo_command(data, p, command))
                return true;

        if (looking_at(&p, "draw arrays "))
                return process_draw_arrays_command(data, p, command);

        if (looking_at(&p, "compute "))
                return process_compute_command(data, p, command);

        if (looking_at(&p, "uniform ubo ")) {
                unsigned values[2];
                if (!parse_desc_set_and_binding(&p, values))
                        goto error;
                return process_set_buffer_subdata(data,
                                                  values[0],
                                                  values[1],
                                                  VR_SCRIPT_BUFFER_TYPE_UBO,
                                                  p,
                                                  command);
        }

        if (looking_at(&p, "ssbo ")) {
                unsigned values[2];

                if (!parse_desc_set_and_binding(&p, values))
                        goto error;

                while (isspace(*p))
                        p++;
                if (!looking_at(&p, "subdata "))
                        goto error;

                return process_set_buffer_subdata(data,
                                                  values[0],
                                                  values[1],
                                                  VR_SCRIPT_BUFFER_TYPE_SSBO,
                                                  p,
                                                  command);
        }

        if (looking_at(&p, "uniform ")) {
                while (isspace(*p))
                        p++;
                enum vr_box_type type;
                if (!parse_value_type(&p,
                                      &type))
                        goto error;
                if (!parse_size_t(&p, &command->set_push_constant.offset))
                        goto error;
                if (!parse_buffer_subdata(data,
                                          &p,
                                          type,
                                          &command->set_push_constant.size,
                                          &command->set_push_constant.data))
                        goto error;
                if (!is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_SET_PUSH_CONSTANT;
                return true;
        }

        if (looking_at(&p, "clear")) {
                if (!is_end(p))
                        goto error;
                command->op = VR_SCRIPT_OP_CLEAR;
                memcpy(command->clear.color,
                       data->clear_color,
                       sizeof data->clear_color);
                command->clear.depth = data->clear_depth;
                command->clear.stencil = data->clear_stencil;
                return true;
        }

error:
        vr_error_message(data->config,
                         "%s:%i: Invalid test command",
                         data->filename,
                         data->line_num);
        return false;
}

static void
set_current_section(struct load_state *data,
                    enum section section)
{
        data->current_section = section;
        data->had_sections |= (1 << section);
}

static bool
is_stage_section(struct load_state *data,
                 const char *start,
                 const char *end)
{
        static const char tail[] = " shader";
        int stage;

        for (stage = 0; stage < VR_SHADER_STAGE_N_STAGES; stage++) {
                int len = strlen(stage_names[stage]);

                if (end - start >= len + (sizeof tail) - 1 &&
                    !memcmp(start, stage_names[stage], len) &&
                    !memcmp(start + len, tail, (sizeof tail) - 1)) {
                        start += len + (sizeof tail) - 1;
                        goto found_stage;
                }
        }

        return false;

found_stage:

        if (is_string(" spirv", start, end))
                data->current_source_type = VR_SCRIPT_SOURCE_TYPE_SPIRV;
        else if (is_string(" binary", start, end))
                data->current_source_type = VR_SCRIPT_SOURCE_TYPE_BINARY;
        else if (start == end)
                data->current_source_type = VR_SCRIPT_SOURCE_TYPE_GLSL;
        else
                return false;

        set_current_section(data, SECTION_SHADER);
        data->current_stage = stage;
        data->buffer.length = 0;
        return true;
}

static bool
start_spirv_shader(struct load_state *data,
                   enum vr_shader_stage stage)
{
        if (!vr_list_empty(&data->script->stages[stage])) {
                vr_error_message(data->config,
                                 "%s:%i: SPIR-V source can not be "
                                 "linked with other shaders in the "
                                 "same stage",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        return true;
}

static bool
is_spirv_shader(enum vr_script_source_type type)
{
        switch (type) {
        case VR_SCRIPT_SOURCE_TYPE_BINARY:
        case VR_SCRIPT_SOURCE_TYPE_SPIRV:
                return true;
        case VR_SCRIPT_SOURCE_TYPE_GLSL:
                return false;
        }

        vr_fatal("Unexpected source type");
}

static bool
process_section_header(struct load_state *data)
{
        if (!end_section(data))
                return false;

        const char *start = (char *) data->line.data + 1;
        const char *end = strchr(start, ']');
        if (end == NULL) {
                vr_error_message(data->config,
                                 "%s:%i: Missing ']'",
                                 data->filename,
                                 data->line_num);
                return false;
        }

        if (is_stage_section(data, start, end)) {
                if (is_spirv_shader(data->current_source_type) &&
                    !start_spirv_shader(data, data->current_stage))
                        return false;

                return true;
        }

        if (is_string("vertex shader passthrough", start, end)) {
                if (!start_spirv_shader(data, VR_SHADER_STAGE_VERTEX))
                        return false;
                set_current_section(data, SECTION_NONE);
                add_shader(data->script,
                           VR_SHADER_STAGE_VERTEX,
                           VR_SCRIPT_SOURCE_TYPE_BINARY,
                           sizeof vertex_shader_passthrough,
                           (const char *) vertex_shader_passthrough);
                return true;
        }

        if (is_string("comment", start, end)) {
                set_current_section(data, SECTION_COMMENT);
                return true;
        }

        if (is_string("require", start, end)) {
                /* The “require” section must come first because the
                 * “test” section uses the window size while parsing
                 * the commands.
                 */
                if ((data->had_sections & ~(1 << SECTION_COMMENT)) != 0) {
                        vr_error_message(data->config,
                                         "%s:%i: [require] must be the first "
                                         "section",
                                         data->filename,
                                         data->line_num);
                        return false;
                }
                set_current_section(data, SECTION_REQUIRE);
                return true;
        }

        if (is_string("test", start, end)) {
                set_current_section(data, SECTION_TEST);
                return true;
        }

        if (is_string("indices", start, end)) {
                set_current_section(data, SECTION_INDICES);
                return true;
        }

        if (is_string("vertex data", start, end)) {
                if (data->script->vertex_data) {
                        vr_error_message(data->config,
                                         "%s:%i: Duplicate vertex data section",
                                         data->filename,
                                         data->line_num);
                        return false;
                }
                set_current_section(data, SECTION_VERTEX_DATA);
                data->buffer.length = 0;
                return true;
        }

        vr_error_message(data->config,
                         "%s:%i: Unknown section “%.*s”",
                         data->filename,
                         data->line_num,
                         (int) (end - start),
                         start);
        return false;
}

static int
hex_value(char ch)
{
        if (ch < '0')
                return -1;
        if (ch <= '9')
                return ch - '0';
        if (ch < 'A')
                return -1;
        if (ch <= 'F')
                return ch - 'A' + 10;
        if (ch < 'a')
                return -1;
        if (ch <= 'f')
                return ch - 'a' + 10;
        return -1;
}

static bool
decode_binary(struct load_state *data,
              const char *line,
              size_t length)
{
        const char *end = line + length;

        while (true) {
                /* Skip spaces and finish if we encounter the end of
                 * the line */
                while (true) {
                        if (line >= end)
                                return true;
                        if (!isspace(*line))
                                break;
                        line++;
                }

                /* Skip comments */
                if (*line == '#')
                        return true;

                /* If it’s not a space then it must be a hex digit */
                int digit = hex_value(*(line++));

                if (digit == -1) {
                        vr_error_message(data->config,
                                         "%s:%i: Invalid character "
                                         "in binary data",
                                         data->filename,
                                         data->line_num);
                        return false;
                }

                uint32_t value = digit;

                while (line < end) {
                        digit = hex_value(*line);
                        if (digit == -1)
                                break;
                        value = (value << 4) | digit;
                        line++;
                }

                vr_buffer_append(&data->buffer, &value, sizeof value);
        }
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
                if (data->current_source_type == VR_SCRIPT_SOURCE_TYPE_BINARY) {
                        if (!decode_binary(data,
                                           (const char *) data->line.data,
                                           data->line.length))
                                return false;
                } else {
                        vr_buffer_append(&data->buffer,
                                         data->line.data,
                                         data->line.length);
                }
                return true;

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

static int
compare_buffer_set_and_binding(const void *a,
                               const void *b)
{
        const struct vr_script_buffer *buffer_a =
                (const struct vr_script_buffer *) a;
        const struct vr_script_buffer *buffer_b =
                (const struct vr_script_buffer *) b;
        if ((int) buffer_a->desc_set == (int) buffer_b->desc_set)
                return ((int) buffer_a->binding - (int) buffer_b->binding);
        return ((int) buffer_a->desc_set - (int) buffer_b->desc_set);
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
process_token_replacements(struct load_state *data)
{
        int count = 0;

        for (int i = 0; i < data->line.length; i++) {
                while (find_replacement(&data->source->token_replacements,
                                        &data->line,
                                        i)) {
                        count++;

                        if (count > 1000) {
                                fprintf(stderr,
                                        "%s:%i: infinite recursion suspected "
                                        "while replacing tokens\n",
                                        data->filename,
                                        data->line_num);
                                return false;
                        }
                }
        }

        return true;
}

static bool
load_script_from_stream(struct load_state *data,
                        struct vr_stream *stream)
{
        bool res = true;

        do {
                int lines_consumed = vr_stream_read_line(stream, &data->line);

                if (lines_consumed == 0)
                        break;

                if (!process_token_replacements(data)) {
                        res = false;
                        break;
                }

                res = process_line(data);

                data->line_num += lines_consumed;
        } while (res);

        if (res)
                res = end_section(data);

        return res;
}

static bool
load_script_from_file(struct load_state *data,
                      const char *filename)
{
        FILE *f = fopen(filename, "r");

        if (f == NULL) {
                vr_error_message(data->config,
                                 "%s: %s",
                                 filename,
                                 strerror(errno));
                return false;
        }

        struct vr_stream stream;

        vr_stream_init_file(&stream, f);
        bool res = load_script_from_stream(data, &stream);
        fclose(f);

        return res;
}

static bool
load_script_from_string(struct load_state *data,
                        const char *string)
{
        struct vr_stream stream;

        vr_stream_init_string(&stream, string);
        bool res = load_script_from_stream(data, &stream);

        return res;
}

struct vr_script *
vr_script_load(const struct vr_config *config,
               const struct vr_source *source)
{
        struct vr_script *script = vr_calloc(sizeof (struct vr_script));
        struct load_state data = {
                .config = config,
                .source = source,
                .line_num = 1,
                .script = script,
                .current_stage = -1,
                .current_section = SECTION_NONE,
                .clear_depth = 1.0f,
                .line = VR_BUFFER_STATIC_INIT,
                .buffer = VR_BUFFER_STATIC_INIT,
                .commands = VR_BUFFER_STATIC_INIT,
                .pipeline_keys = VR_BUFFER_STATIC_INIT,
                .extensions = VR_BUFFER_STATIC_INIT,
                .buffers = VR_BUFFER_STATIC_INIT,
                .tolerance = {
                        .value = {
                                DEFAULT_TOLERANCE,
                                DEFAULT_TOLERANCE,
                                DEFAULT_TOLERANCE,
                                DEFAULT_TOLERANCE,
                        },
                        .is_percent = false,
                },
        };

        vr_pipeline_key_init(&data.current_key);

        script->window_format.width = 250;
        script->window_format.height = 250;
        script->window_format.color_format =
                vr_format_lookup_by_vk_format(VK_FORMAT_B8G8R8A8_UNORM);
        assert(script->window_format.color_format != NULL);

        vr_buffer_set_length(&data.extensions, sizeof (const char *));
        memset(data.extensions.data, 0, data.extensions.length);

        for (int stage = 0; stage < VR_SHADER_STAGE_N_STAGES; stage++)
                vr_list_init(&script->stages[stage]);

        bool res = false;

        switch (source->type) {
        case VR_SOURCE_TYPE_FILE:
                data.filename = source->string;
                script->filename = vr_strdup(data.filename);
                res = load_script_from_file(&data, source->string);
                break;

        case VR_SOURCE_TYPE_STRING:
                data.filename = "(string script)";
                script->filename = vr_strdup(data.filename);
                res = load_script_from_string(&data, source->string);
                break;
        }

        script->commands = (struct vr_script_command *) data.commands.data;
        script->n_commands = (data.commands.length /
                              sizeof (struct vr_script_command));
        script->pipeline_keys =
                (struct vr_pipeline_key *) data.pipeline_keys.data;
        script->n_pipeline_keys = (data.pipeline_keys.length /
                                   sizeof (struct vr_pipeline_key));
        script->extensions =
                (const char *const *) data.extensions.data;
        script->indices = (uint16_t *) data.indices.data;
        script->n_indices =
                data.indices.length / sizeof (uint16_t);

        script->buffers = (struct vr_script_buffer *) data.buffers.data;
        script->n_buffers = (data.buffers.length /
                             sizeof (struct vr_script_buffer));
        qsort(script->buffers,
              script->n_buffers,
              sizeof script->buffers[0],
              compare_buffer_set_and_binding);

        vr_buffer_destroy(&data.buffer);
        vr_buffer_destroy(&data.line);
        vr_pipeline_key_destroy(&data.current_key);

        if (res) {
                return script;
        } else {
                vr_script_free(script);
                return NULL;
        }
}

void
vr_script_free(struct vr_script *script)
{
        int stage;
        struct vr_script_shader *shader, *tmp;

        for (int i = 0; i < script->n_commands; i++) {
                struct vr_script_command *command = script->commands + i;
                if (command->op == VR_SCRIPT_OP_SET_BUFFER_SUBDATA)
                        vr_free(command->set_buffer_subdata.data);
                else if (command->op == VR_SCRIPT_OP_SET_PUSH_CONSTANT)
                        vr_free(command->set_push_constant.data);
                else if (command->op == VR_SCRIPT_OP_PROBE_SSBO)
                        vr_free(command->probe_ssbo.value);
        }

        for (stage = 0; stage < VR_SHADER_STAGE_N_STAGES; stage++) {
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

        for (int i = 0; i < script->n_pipeline_keys; i++)
                vr_pipeline_key_destroy(script->pipeline_keys + i);
        vr_free(script->pipeline_keys);

        vr_free(script->buffers);

        if (script->extensions != NULL) {
                for (const char *const *ext = script->extensions; *ext; ext++)
                        vr_free((char *) *ext);

                vr_free((void *) script->extensions);
        }

        vr_free(script);
}
