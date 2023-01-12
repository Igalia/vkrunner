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

enum vr_pipeline_key_source {
        VR_PIPELINE_KEY_SOURCE_RECTANGLE,
        VR_PIPELINE_KEY_SOURCE_VERTEX_DATA
};

struct vr_pipeline_key;

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

void
vr_pipeline_key_set_type(struct vr_pipeline_key *key,
                         enum vr_pipeline_key_type type);

enum vr_pipeline_key_type
vr_pipeline_key_get_type(const struct vr_pipeline_key *key);

void
vr_pipeline_key_set_source(struct vr_pipeline_key *key,
                           enum vr_pipeline_key_source source);

enum vr_pipeline_key_source
vr_pipeline_key_get_source(const struct vr_pipeline_key *key);

void
vr_pipeline_key_set_topology(struct vr_pipeline_key *key,
                             VkPrimitiveTopology topology);

void
vr_pipeline_key_set_patch_control_points(struct vr_pipeline_key *key,
                                         int patch_control_points);

bool
vr_pipeline_key_equal(const struct vr_pipeline_key *a,
                      const struct vr_pipeline_key *b);

struct vr_pipeline_key *
vr_pipeline_key_copy(const struct vr_pipeline_key *src);

void
vr_pipeline_key_set_entrypoint(struct vr_pipeline_key *key,
                               enum vr_shader_stage stage,
                               const char *entrypoint);

/* Caller should free the string */
char *
vr_pipeline_key_get_entrypoint(const struct vr_pipeline_key *key,
                               enum vr_shader_stage stage);

enum vr_pipeline_key_set_result
vr_pipeline_key_set(struct vr_pipeline_key *key,
                    const char *name,
                    const char *value);

/* This awkward struct is to make freeing the create info from Rust a
 * bit easier.
 */
struct vr_pipeline_key_create_info {
        VkGraphicsPipelineCreateInfo *create_info;
        size_t len;
};

void
vr_pipeline_key_to_create_info(const struct vr_pipeline_key *key,
                               struct vr_pipeline_key_create_info *ci);
void
vr_pipeline_key_destroy_create_info(struct vr_pipeline_key_create_info *ci);

void
vr_pipeline_key_free(struct vr_pipeline_key *key);

#endif /* VR_PIPELINE_KEY_H */
