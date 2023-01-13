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

#include "vr-pipeline-key.h"
#include "vr-util.h"
#include "vr-char.h"
#include "vr-hex.h"

#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <errno.h>

union vr_pipeline_key_value {
        int i;
        float f;
};

enum vr_pipeline_key_value_type {
        VR_PIPELINE_KEY_VALUE_TYPE_BOOL,
        VR_PIPELINE_KEY_VALUE_TYPE_INT,
        VR_PIPELINE_KEY_VALUE_TYPE_FLOAT
};

struct vr_pipeline_key {
        enum vr_pipeline_key_type type;
        enum vr_pipeline_key_source source;

#define VR_PIPELINE_STRUCT_BEGIN(m)
#define VR_PIPELINE_STRUCT_BEGIN2(m1, s2, m2)
#define VR_PIPELINE_PROP(t, s, n) union vr_pipeline_key_value n;
#define VR_PIPELINE_PROP_NAME(t, s, m, n) VR_PIPELINE_PROP(t, s, n)
#define VR_PIPELINE_STRUCT_END()
#include "vr-pipeline-properties.h"
#undef VR_PIPELINE_STRUCT_BEGIN
#undef VR_PIPELINE_STRUCT_BEGIN2
#undef VR_PIPELINE_PROP
#undef VR_PIPELINE_PROP_NAME
#undef VR_PIPELINE_STRUCT_END

        /* This must be the last entry so that the rest can be
         * compared with a simple memcmp in vr_pipeline_key_equal */
        char *entrypoints[VR_SHADER_STAGE_N_STAGES];
};

static const struct vr_pipeline_key
base_key = {
        .type = VR_PIPELINE_KEY_TYPE_GRAPHICS,
        .topology = { .i = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP },
        .polygonMode = { .i = VK_POLYGON_MODE_FILL },
        .cullMode = { .i = VK_CULL_MODE_NONE },
        .frontFace = { .i = VK_FRONT_FACE_COUNTER_CLOCKWISE },
        .lineWidth = { .f = 1.0f },
        .blendEnable = { .i = false },
        .colorWriteMask = { .i = (VK_COLOR_COMPONENT_R_BIT |
                                  VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT |
                                  VK_COLOR_COMPONENT_A_BIT) },
        .depthTestEnable = { .i = false },
        .depthWriteEnable = { .i = false },
        .depthCompareOp = { .i = VK_COMPARE_OP_LESS },
        .stencilTestEnable = { .i = false },
        .front_failOp = { .i = VK_STENCIL_OP_KEEP },
        .front_passOp = { .i = VK_STENCIL_OP_KEEP },
        .front_depthFailOp = { .i = VK_STENCIL_OP_KEEP },
        .front_compareOp = { .i = VK_COMPARE_OP_ALWAYS },
        .front_compareMask = { .i = UINT32_MAX },
        .front_writeMask = { .i = UINT32_MAX },
        .front_reference = { .i = 0 },
        .back_failOp = { .i = VK_STENCIL_OP_KEEP },
        .back_passOp = { .i = VK_STENCIL_OP_KEEP },
        .back_depthFailOp = { .i = VK_STENCIL_OP_KEEP },
        .back_compareOp = { .i = VK_COMPARE_OP_ALWAYS },
        .back_compareMask = { .i = UINT32_MAX },
        .back_writeMask = { .i = UINT32_MAX },
        .back_reference = { .i = 0 },
};

struct vr_enum {
        const char *name;
        int value;
};

#include "vr-enum-table.h"

struct vr_pipeline_key_prop {
        const char *name;
        size_t member_offset;
        size_t key_offset;
        enum vr_pipeline_key_value_type type;
};

struct vr_pipeline_key_struct {
        const size_t *pointer_offsets;
        const struct vr_pipeline_key_prop *members;
};

static const struct vr_pipeline_key_struct
structs[] = {
#define VR_PIPELINE_STRUCT_BEGIN(m)                             \
        {                                                       \
        .pointer_offsets = (const size_t[]) {                   \
                offsetof(VkGraphicsPipelineCreateInfo, m),      \
                SIZE_MAX },                                     \
        .members = (const struct vr_pipeline_key_prop[]) {
#define VR_PIPELINE_STRUCT_BEGIN2(m1, s2, m2)                   \
        {                                                       \
        .pointer_offsets = (const size_t[]) {                   \
                offsetof(VkGraphicsPipelineCreateInfo, m1),     \
                offsetof(s2, m2),                               \
                SIZE_MAX },                                     \
        .members = (const struct vr_pipeline_key_prop[]) {
#define VR_PIPELINE_PROP(t, s, n)                               \
        { .name = #n,                                           \
          .member_offset = offsetof(s, n),                      \
          .key_offset = offsetof(struct vr_pipeline_key, n),    \
          .type = VR_PIPELINE_KEY_VALUE_TYPE_ ## t,             \
        },
#define VR_PIPELINE_PROP_NAME(t, s, m, n)                       \
        { .name = #m,                                           \
          .member_offset = offsetof(s, m),                      \
          .key_offset = offsetof(struct vr_pipeline_key, n),    \
          .type = VR_PIPELINE_KEY_VALUE_TYPE_ ## t,             \
        },
#define VR_PIPELINE_STRUCT_END()                  \
        { .name = NULL }                          \
        } \
        },
#include "vr-pipeline-properties.h"
#undef VR_PIPELINE_STRUCT_BEGIN
#undef VR_PIPELINE_STRUCT_BEGIN2
#undef VR_PIPELINE_PROP
#undef VR_PIPELINE_PROP_NAME
#undef VR_PIPELINE_STRUCT_END
};

struct vr_pipeline_key *
vr_pipeline_key_new(void)
{
        return vr_memdup(&base_key, sizeof base_key);
}

struct vr_pipeline_key *
vr_pipeline_key_copy(const struct vr_pipeline_key *src)
{
        struct vr_pipeline_key *dest = vr_memdup(src, sizeof *src);

        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++) {
                if (src->entrypoints[i])
                        dest->entrypoints[i] = vr_strdup(src->entrypoints[i]);
        }

        return dest;
}

void
vr_pipeline_key_set_type(struct vr_pipeline_key *key,
                         enum vr_pipeline_key_type type)
{
        key->type = type;
}

enum vr_pipeline_key_type
vr_pipeline_key_get_type(const struct vr_pipeline_key *key)
{
        return key->type;
}

void
vr_pipeline_key_set_source(struct vr_pipeline_key *key,
                           enum vr_pipeline_key_source source)
{
        key->source = source;
}

enum vr_pipeline_key_source
vr_pipeline_key_get_source(const struct vr_pipeline_key *key)
{
        return key->source;
}

void
vr_pipeline_key_set_topology(struct vr_pipeline_key *key,
                             VkPrimitiveTopology topology)
{
        key->topology.i = topology;
}

void
vr_pipeline_key_set_patch_control_points(struct vr_pipeline_key *key,
                                         int patch_control_points)
{
        key->patchControlPoints.i = patch_control_points;
}

static const char *
get_entrypoint(const struct vr_pipeline_key *key,
               enum vr_shader_stage stage)
{
        if (key->entrypoints[stage])
                return key->entrypoints[stage];
        else
                return "main";
}

bool
vr_pipeline_key_equal(const struct vr_pipeline_key *a,
                      const struct vr_pipeline_key *b)
{
        if (a->type != b->type)
                return false;

        switch (a->type) {
        case VR_PIPELINE_KEY_TYPE_GRAPHICS:
                if (memcmp(a, b, offsetof(struct vr_pipeline_key, entrypoints)))
                        return false;

                for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++) {
                        if (i == VR_SHADER_STAGE_COMPUTE)
                                continue;
                        if (strcmp(get_entrypoint(a, i), get_entrypoint(b, i)))
                                return false;
                }

                return true;

        case VR_PIPELINE_KEY_TYPE_COMPUTE: {
                const enum vr_shader_stage stage = VR_SHADER_STAGE_COMPUTE;

                if (strcmp(get_entrypoint(a, stage), get_entrypoint(b, stage)))
                        return false;
                return true;
        }
        }

        vr_fatal("Unexpected shader stage");
}

static union vr_pipeline_key_value *
find_prop(struct vr_pipeline_key *key,
          const char *name,
          enum vr_pipeline_key_value_type *type_out)
{
        for (int struct_num = 0;
             struct_num < VR_N_ELEMENTS(structs);
             struct_num++) {
                const struct vr_pipeline_key_struct *s = structs + struct_num;
                for (const struct vr_pipeline_key_prop *prop = s->members;
                     prop->name;
                     prop++) {
                        if (!strcmp(prop->name, name)) {
                                *type_out = prop->type;
                                return ((union vr_pipeline_key_value *)
                                        ((uint8_t *) key + prop->key_offset));
                        }
                }
        }

        return NULL;
}

static bool
is_end(const char *p)
{
        while (*p && vr_char_is_space(*p))
                p++;

        return *p == '\0';
}

static bool
parse_int(const char **p,
          int *out)
{
        while (vr_char_is_space(**p))
                (*p)++;

        errno = 0;
        char *tail;
        long v = strtol(*p, &tail, 10);
        if (errno != 0 || tail == *p || v < INT_MIN || v > INT_MAX)
                return false;

        *out = (int) v;
        *p = tail;

        return true;
}

static bool
parse_float(const char **p,
            float *out)
{
        char *tail;

        while (vr_char_is_space(**p))
                (*p)++;

        errno = 0;
        *out = vr_hex_strtof(*p, &tail);
        if (errno != 0 || tail == *p)
                return false;

        *p = tail;

        return true;
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
process_bool_property(union vr_pipeline_key_value *value,
                      const char *p)
{
        if (looking_at(&p, "true"))
                value->i = true;
        else if (looking_at(&p, "false"))
                value->i = false;
        else if (!parse_int(&p, &value->i))
                return false;

        return is_end(p);
}

static bool
lookup_enum(const char *name,
            int *value)
{
        int begin = 0, end = VR_N_ELEMENTS(enums);

        while (end > begin) {
                int mid = (begin + end) / 2;
                int comp = strcmp(name, enums[mid].name);

                if (comp > 0) {
                        begin = mid + 1;
                } else if (comp < 0) {
                        end = mid;
                } else {
                        *value = enums[mid].value;
                        return true;
                }
        }

        return false;
}

static bool
process_int_property(union vr_pipeline_key_value *value,
                     const char *p)
{
        value->i = 0;

        while (true) {
                int this_int;

                while (vr_char_is_space(*p))
                        p++;

                if (parse_int(&p, &this_int)) {
                        value->i |= this_int;
                } else if (vr_char_is_alnum(*p)) {
                        const char *end = p + 1;
                        while (vr_char_is_alnum(*end) || *end == '_')
                                end++;
                        char *enum_name = vr_strndup(p, end - p);
                        bool is_enum = lookup_enum(enum_name, &this_int);
                        free(enum_name);

                        if (!is_enum)
                                return false;

                        value->i |= this_int;
                        p = end;
                } else {
                        return false;
                }

                if (is_end(p))
                        break;

                while (vr_char_is_space(*p))
                        p++;

                if (*p != '|')
                        return false;
                p++;
        }

        return true;
}

static bool
process_float_property(union vr_pipeline_key_value *value,
                       const char *p)
{
        while (vr_char_is_space(*p))
                p++;

        return parse_float(&p, &value->f) && is_end(p);
}

enum vr_pipeline_key_set_result
vr_pipeline_key_set(struct vr_pipeline_key *key,
                    const char *name,
                    const char *value)
{
        enum vr_pipeline_key_value_type type;
        union vr_pipeline_key_value *key_value = find_prop(key, name, &type);

        if (key_value == NULL)
                return VR_PIPELINE_KEY_SET_RESULT_NOT_FOUND;

        bool process_result = false;

        switch (type) {
        case VR_PIPELINE_KEY_VALUE_TYPE_BOOL:
                process_result = process_bool_property(key_value, value);
                goto found_type;
        case VR_PIPELINE_KEY_VALUE_TYPE_INT:
                process_result = process_int_property(key_value, value);
                goto found_type;
        case VR_PIPELINE_KEY_VALUE_TYPE_FLOAT:
                process_result = process_float_property(key_value, value);
                goto found_type;
        }

        vr_fatal("Unknown pipeline property type");

found_type:
        return (process_result ?
                VR_PIPELINE_KEY_SET_RESULT_OK :
                VR_PIPELINE_KEY_SET_RESULT_INVALID_VALUE);
}

void
vr_pipeline_key_set_entrypoint(struct vr_pipeline_key *key,
                               enum vr_shader_stage stage,
                               const char *entrypoint)
{
        vr_free(key->entrypoints[stage]);
        key->entrypoints[stage] = vr_strdup(entrypoint);
}

char *
vr_pipeline_key_get_entrypoint(const struct vr_pipeline_key *key,
                               enum vr_shader_stage stage)
{
        return vr_strdup(get_entrypoint(key, stage));
}

static size_t
type_size(enum vr_pipeline_key_value_type type)
{
        switch (type) {
        case VR_PIPELINE_KEY_VALUE_TYPE_BOOL:
        case VR_PIPELINE_KEY_VALUE_TYPE_INT:
                return sizeof (int);
        case VR_PIPELINE_KEY_VALUE_TYPE_FLOAT:
                return sizeof (float);
        }

        vr_fatal("Unknown vr_pipeline_key_value_type");
}

void
vr_pipeline_key_to_create_info(const struct vr_pipeline_key *key,
                               VkGraphicsPipelineCreateInfo *create_info)
{
        for (int struct_num = 0;
             struct_num < VR_N_ELEMENTS(structs);
             struct_num++) {
                const struct vr_pipeline_key_struct *s = structs + struct_num;
                uint8_t *struct_start = (uint8_t *) create_info;

                for (const size_t *off = s->pointer_offsets;
                     *off != SIZE_MAX;
                     off++)
                        struct_start = *((uint8_t **) (struct_start + *off));

                for (const struct vr_pipeline_key_prop *prop = s->members;
                     prop->name;
                     prop++) {
                        const void *value = ((const uint8_t *) key +
                                             prop->key_offset);
                        memcpy(struct_start + prop->member_offset,
                               value,
                               type_size(prop->type));
                }
        }
}

void
vr_pipeline_key_free(struct vr_pipeline_key *key)
{
        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++)
                vr_free(key->entrypoints[i]);

        vr_free(key);
}
