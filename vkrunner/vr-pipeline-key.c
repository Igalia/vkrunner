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

#include <string.h>
#include <limits.h>

static const struct vr_pipeline_key
base_key = {
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

void
vr_pipeline_key_init(struct vr_pipeline_key *key)
{
        *key = base_key;
}

void
vr_pipeline_key_copy(struct vr_pipeline_key *dest,
                     const struct vr_pipeline_key *src)
{
        *dest = *src;
}

bool
vr_pipeline_key_equal(const struct vr_pipeline_key *a,
                      const struct vr_pipeline_key *b)
{
        return memcmp(a, b, sizeof *a) == 0;
}

union vr_pipeline_key_value *
vr_pipeline_key_lookup(struct vr_pipeline_key *key,
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

bool
vr_pipeline_key_lookup_enum(const char *name,
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

void
vr_pipeline_key_destroy(struct vr_pipeline_key *key)
{
}
