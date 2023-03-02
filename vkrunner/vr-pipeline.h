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

#ifndef VR_PIPELINE_H
#define VR_PIPELINE_H

#include <stdlib.h>

#include "vr-script-private.h"
#include "vr-window.h"
#include "vr-config.h"

struct vr_pipeline_vertex {
        float x, y, z;
};

struct vr_pipeline *
vr_pipeline_create(const struct vr_config *config,
                   struct vr_window *window,
                   const struct vr_script *script);

size_t
vr_pipeline_get_n_desc_sets(const struct vr_pipeline *pipeline);

VkShaderStageFlagBits
vr_pipeline_get_stages(const struct vr_pipeline *pipeline);

VkPipelineLayout
vr_pipeline_get_layout(const struct vr_pipeline *pipeline);

const VkPipeline *
vr_pipeline_get_pipelines(const struct vr_pipeline *pipeline);

size_t
vr_pipeline_get_n_pipelines(const struct vr_pipeline *pipeline);

VkDescriptorPool
vr_pipeline_get_descriptor_pool(const struct vr_pipeline *pipeline);

const VkDescriptorSetLayout *
vr_pipeline_get_descriptor_set_layouts(const struct vr_pipeline *pipeline);

void
vr_pipeline_free(struct vr_pipeline *pipeline);

#endif /* VR_PIPELINE_H */
