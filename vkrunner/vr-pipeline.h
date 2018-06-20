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

#include "vr-script.h"
#include "vr-window.h"
#include "vr-config-private.h"
#include "vr-pipeline-key.h"

struct vr_pipeline {
        struct vr_window *window;
        VkPipelineLayout layout;
        VkDescriptorSetLayout descriptor_set_layout;
        struct vr_pipeline_key *keys;
        int n_pipelines;
        VkPipeline *pipelines;
        VkPipelineCache pipeline_cache;
        VkPipeline compute_pipeline;
        VkShaderModule modules[VR_SCRIPT_N_STAGES];
        VkShaderStageFlagBits stages;
};

struct vr_pipeline_vertex {
        float x, y, z;
};

struct vr_pipeline *
vr_pipeline_create(const struct vr_config *config,
                   struct vr_window *window,
                   const struct vr_script *script);

VkPipeline
vr_pipeline_for_command(struct vr_pipeline *pipeline,
                        const struct vr_script_command *command);

void
vr_pipeline_free(struct vr_pipeline *pipeline);

#endif /* VR_PIPELINE_H */
