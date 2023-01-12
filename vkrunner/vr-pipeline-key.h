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

#ifndef VR_PIPELINE_KEY_H
#define VR_PIPELINE_KEY_H

#include <stdbool.h>
#include "vr-vk.h"
#include "vr-shader-stage.h"

enum vr_pipeline_key_type {
        VR_PIPELINE_KEY_TYPE_GRAPHICS,
        VR_PIPELINE_KEY_TYPE_COMPUTE
};

union vr_pipeline_key_value {
        int i;
        float f;
};

enum vr_pipeline_key_value_type {
        VR_PIPELINE_KEY_VALUE_TYPE_BOOL,
        VR_PIPELINE_KEY_VALUE_TYPE_INT,
        VR_PIPELINE_KEY_VALUE_TYPE_FLOAT
};

enum vr_pipeline_key_source {
        VR_PIPELINE_KEY_SOURCE_RECTANGLE,
        VR_PIPELINE_KEY_SOURCE_VERTEX_DATA
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

enum vr_pipeline_key_set_result {
        /* The property was successfully changed */
        VR_PIPELINE_KEY_SET_RESULT_OK,
        /* The key was not found */
        VR_PIPELINE_KEY_SET_RESULT_NOT_FOUND,
        /* The value was invalid */
        VR_PIPELINE_KEY_SET_RESULT_INVALID_VALUE,
};

struct vr_pipeline_key *
vr_pipeline_key_new(void);

bool
vr_pipeline_key_equal(const struct vr_pipeline_key *a,
                      const struct vr_pipeline_key *b);

struct vr_pipeline_key *
vr_pipeline_key_copy(const struct vr_pipeline_key *src);

void
vr_pipeline_key_set_entrypoint(struct vr_pipeline_key *key,
                               enum vr_shader_stage stage,
                               const char *entrypoint);

const char *
vr_pipeline_key_get_entrypoint(const struct vr_pipeline_key *key,
                               enum vr_shader_stage stage);

enum vr_pipeline_key_set_result
vr_pipeline_key_set(struct vr_pipeline_key *key,
                    const char *name,
                    const char *value);

void
vr_pipeline_key_to_create_info(const struct vr_pipeline_key *key,
                               VkGraphicsPipelineCreateInfo *create_info);

void
vr_pipeline_key_free(struct vr_pipeline_key *key);

#endif /* VR_PIPELINE_KEY_H */
