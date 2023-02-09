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

#include "vr-pipeline.h"
#include "vr-util.h"
#include "vr-script-private.h"
#include "vr-error-message.h"
#include "vr-buffer.h"
#include "vr-format-private.h"
#include "vr-compiler.h"
#include "vr-pipeline-key.h"

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <limits.h>

struct vr_pipeline {
        struct vr_window *window;
        VkPipelineLayout layout;
        VkDescriptorPool descriptor_pool;
        VkDescriptorSetLayout *descriptor_set_layout;
        unsigned n_desc_sets;
        int n_pipelines;
        VkPipeline *pipelines;
        VkPipelineCache pipeline_cache;
        VkShaderModule modules[VR_SHADER_STAGE_N_STAGES];
        VkShaderStageFlagBits stages;
};

struct desc_set_bindings_info {
        VkDescriptorSetLayoutBinding *bindings;
        unsigned n_bindings;
        unsigned desc_set;
};

static const VkPipelineMultisampleStateCreateInfo
base_multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
};


static void
set_up_attrib_cb(const struct vr_vbo_attrib *attrib,
                 void *user_data)
{
        VkVertexInputAttributeDescription **attrib_desc_ptr = user_data;
        VkVertexInputAttributeDescription *attrib_desc = *attrib_desc_ptr;

        attrib_desc->location = vr_vbo_attrib_get_location(attrib);
        attrib_desc->binding = 0;
        attrib_desc->format = vr_vbo_attrib_get_format(attrib)->vk_format;
        attrib_desc->offset = vr_vbo_attrib_get_offset(attrib);

        (*attrib_desc_ptr)++;
}

static void
set_vertex_input_state(const struct vr_script *script,
                       VkPipelineVertexInputStateCreateInfo *state,
                       const struct vr_pipeline_key *key)
{
        memset(state, 0, sizeof *state);

        state->sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        enum vr_pipeline_key_source key_source =
                vr_pipeline_key_get_source(key);

        const struct vr_vbo *vertex_data = vr_script_get_vertex_data(script);

        if (key_source == VR_PIPELINE_KEY_SOURCE_VERTEX_DATA &&
            vertex_data == NULL)
                return;

        VkVertexInputBindingDescription *input_binding =
                vr_calloc(sizeof *input_binding);

        input_binding[0].binding = 0;
        input_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        state->vertexBindingDescriptionCount = 1;
        state->pVertexBindingDescriptions = input_binding;

        if (key_source == VR_PIPELINE_KEY_SOURCE_RECTANGLE) {
                VkVertexInputAttributeDescription *attrib =
                        vr_calloc(sizeof *attrib);
                state->vertexAttributeDescriptionCount = 1;
                state->pVertexAttributeDescriptions = attrib;

                input_binding[0].stride =
                        sizeof (struct vr_pipeline_vertex);

                attrib->location = 0;
                attrib->binding = 0;
                attrib->format = VK_FORMAT_R32G32B32_SFLOAT;
                attrib->offset = offsetof(struct vr_pipeline_vertex, x);

                return;
        }

        int n_attribs = vr_vbo_get_num_attribs(vertex_data);
        VkVertexInputAttributeDescription *attrib_desc =
                vr_calloc((sizeof *attrib_desc) * n_attribs);

        state->vertexAttributeDescriptionCount = n_attribs;
        state->pVertexAttributeDescriptions = attrib_desc;
        input_binding[0].stride = vr_vbo_get_stride(vertex_data);

        vr_vbo_for_each_attrib(vertex_data,
                               set_up_attrib_cb,
                               &attrib_desc);
}

static VkPipeline
create_vk_pipeline(struct vr_pipeline *pipeline,
                   const struct vr_script *script,
                   const struct vr_pipeline_key *key,
                   bool allow_derivatives,
                   VkPipeline parent_pipeline)
{
        struct vr_window *window = pipeline->window;
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkResult res;
        int num_stages = 0;

        struct vr_pipeline_key_create_info create_info_data;

        vr_pipeline_key_to_create_info(key, &create_info_data);

        VkPipelineShaderStageCreateInfo stages[VR_SHADER_STAGE_N_STAGES];
        memset(&stages, 0, sizeof stages);

        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++) {
                if (i == VR_SHADER_STAGE_COMPUTE ||
                    pipeline->modules[i] == VK_NULL_HANDLE)
                        continue;
                stages[num_stages].sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[num_stages].stage = VK_SHADER_STAGE_VERTEX_BIT << i;
                stages[num_stages].module = pipeline->modules[i];
                stages[num_stages].pName =
                        vr_pipeline_key_get_entrypoint(key, i);
                num_stages++;
        }

        const struct vr_window_format *window_format =
                vr_window_get_format(window);

        VkViewport viewports[] = {
                {
                        .width = window_format->width,
                        .height = window_format->height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                }
        };
        VkRect2D scissors[] = {
                {
                        .extent = { window_format->width,
                                    window_format->height }
                }
        };
        VkPipelineViewportStateCreateInfo viewport_state = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = VR_N_ELEMENTS(viewports),
                .pViewports = viewports,
                .scissorCount = VR_N_ELEMENTS(scissors),
                .pScissors = scissors
        };

        VkPipelineVertexInputStateCreateInfo vertex_input_state;
        set_vertex_input_state(script, &vertex_input_state, key);

        VkGraphicsPipelineCreateInfo *info = create_info_data.create_info;

        info->pViewportState = &viewport_state;
        info->pMultisampleState = &base_multisample_state;
        info->subpass = 0;
        info->basePipelineHandle = parent_pipeline;
        info->basePipelineIndex = -1;

        info->stageCount = num_stages;
        info->pStages = stages;
        info->pVertexInputState = &vertex_input_state;
        info->layout = pipeline->layout;
        info->renderPass = vr_window_get_render_pass(window, true);

        if (allow_derivatives)
                info->flags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
        if (parent_pipeline)
                info->flags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;

        if (!(pipeline->stages & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)))
                info->pTessellationState = NULL;

        VkPipeline vk_pipeline;

        res = vkfn->vkCreateGraphicsPipelines(vr_window_get_device(window),
                                              pipeline->pipeline_cache,
                                              1, /* nCreateInfos */
                                              info,
                                              NULL, /* allocator */
                                              &vk_pipeline);

        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++)
                vr_free((char *) stages[i].pName);

        vr_free((void *) vertex_input_state.pVertexBindingDescriptions);
        vr_free((void *) vertex_input_state.pVertexAttributeDescriptions);

        vr_pipeline_key_destroy_create_info(&create_info_data);

        if (res != VK_SUCCESS) {
                vr_error_message(vr_window_get_config(window),
                                 "Error creating VkPipeline");
                return VK_NULL_HANDLE;
        }

        return vk_pipeline;
}

static VkPipeline
create_compute_pipeline(struct vr_pipeline *pipeline,
                        const struct vr_pipeline_key *key)
{
        struct vr_window *window = pipeline->window;
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        char *entrypoint =
                vr_pipeline_key_get_entrypoint(key, VR_SHADER_STAGE_COMPUTE);
        VkResult res;

        VkComputePipelineCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,

                .stage = {
                        .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                        .module =
                        pipeline->modules[VR_SHADER_STAGE_COMPUTE],
                        .pName = entrypoint
                },
                .layout = pipeline->layout,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = -1,
        };

        VkPipeline vk_pipeline;

        res = vkfn->vkCreateComputePipelines(vr_window_get_device(window),
                                             pipeline->pipeline_cache,
                                             1, /* nCreateInfos */
                                             &info,
                                             NULL, /* allocator */
                                             &vk_pipeline);

        vr_free(entrypoint);

        if (res != VK_SUCCESS) {
                vr_error_message(vr_window_get_config(window),
                                 "Error creating VkPipeline");
                return VK_NULL_HANDLE;
        }

        return vk_pipeline;
}

static size_t
get_push_constant_size(const struct vr_script *script)
{
        size_t max = 0;

        const struct vr_script_command *commands;
        size_t n_commands;

        vr_script_get_commands(script, &commands, &n_commands);

        for (int i = 0; i < n_commands; i++) {
                const struct vr_script_command *command = commands + i;
                if (command->op != VR_SCRIPT_OP_SET_PUSH_CONSTANT)
                        continue;

                size_t end = (command->set_push_constant.offset +
                              command->set_push_constant.size);

                if (end > max)
                        max = end;
        }

        return max;
}

static VkPipelineLayout
create_vk_layout(struct vr_pipeline *pipeline,
                 const struct vr_script *script)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(pipeline->window);
        VkResult res;

        VkPushConstantRange push_constant_range = {
                .stageFlags = pipeline->stages,
                .offset = 0,
                .size = get_push_constant_size(script)
        };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };

        if (push_constant_range.size > 0) {
                pipeline_layout_create_info.pushConstantRangeCount = 1;
                pipeline_layout_create_info.pPushConstantRanges =
                        &push_constant_range;
        }

        if (pipeline->descriptor_set_layout) {
                pipeline_layout_create_info.setLayoutCount =
                        pipeline->n_desc_sets;
                pipeline_layout_create_info.pSetLayouts =
                        pipeline->descriptor_set_layout;
        }

        VkDevice device = vr_window_get_device(pipeline->window);

        VkPipelineLayout layout;
        res = vkfn->vkCreatePipelineLayout(device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           &layout);
        if (res != VK_SUCCESS) {
                vr_error_message(vr_window_get_config(pipeline->window),
                                 "Error creating pipeline layout");
                return VK_NULL_HANDLE;
        }

        return layout;
}

static bool
create_vk_descriptor_set_layout(struct vr_pipeline *pipeline,
                                const struct vr_script *script)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(pipeline->window);
        VkResult res;
        bool ret = false;
        const struct vr_script_buffer *buffers;
        size_t n_buffers;

        vr_script_get_buffers(script, &buffers, &n_buffers);

        assert(n_buffers && buffers);

        VkDescriptorSetLayoutBinding *bindings =
                vr_calloc(sizeof (*bindings) * n_buffers);
        struct desc_set_bindings_info *info =
                vr_alloc(sizeof *info * n_buffers);
        size_t n_used_desc_sets = 0;
        unsigned prev_desc_set = UINT_MAX;

        unsigned n_ubo = 0;
        unsigned n_ssbo = 0;

        for (unsigned i = 0; i < n_buffers; i++) {
                const struct vr_script_buffer *buffer = buffers + i;
                VkDescriptorType descriptor_type;
                switch (buffer->type) {
                case VR_SCRIPT_BUFFER_TYPE_UBO:
                        descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        ++n_ubo;
                        goto found_type;
                case VR_SCRIPT_BUFFER_TYPE_SSBO:
                        descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        ++n_ssbo;
                        goto found_type;
                }
                vr_fatal("Unexpected buffer type");
        found_type:
                bindings[i].binding = buffer->binding;
                bindings[i].descriptorType = descriptor_type;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = pipeline->stages;

                if (prev_desc_set != buffer->desc_set) {
                        if (prev_desc_set != UINT_MAX) {
                                info[n_used_desc_sets].n_bindings =
                                        &bindings[i] -
                                        info[n_used_desc_sets].bindings;
                                ++n_used_desc_sets;
                        }
                        info[n_used_desc_sets].desc_set = buffer->desc_set;
                        info[n_used_desc_sets].bindings = &bindings[i];
                        prev_desc_set = buffer->desc_set;
                }
        }
        info[n_used_desc_sets].n_bindings =
                &bindings[n_buffers] - info[n_used_desc_sets].bindings;
        ++n_used_desc_sets;

        size_t n_desc_sets = info[n_used_desc_sets - 1].desc_set + 1;

        VkDescriptorPoolSize pool_sizes[2];
        uint32_t n_pool_sizes = 0;
        if (n_ubo) {
                pool_sizes[n_pool_sizes].type =
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                pool_sizes[n_pool_sizes].descriptorCount = n_ubo;
                n_pool_sizes++;
        }
        if (n_ssbo) {
                pool_sizes[n_pool_sizes].type =
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                pool_sizes[n_pool_sizes].descriptorCount = n_ssbo;
                n_pool_sizes++;
        }

        VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets = n_desc_sets,
                .poolSizeCount = n_pool_sizes,
                .pPoolSizes = pool_sizes
        };

        VkDevice device = vr_window_get_device(pipeline->window);

        res = vkfn->vkCreateDescriptorPool(device,
                                           &descriptor_pool_create_info,
                                           NULL, /* allocator */
                                           &pipeline->descriptor_pool);
        if (res != VK_SUCCESS) {
                vr_error_message(vr_window_get_config(pipeline->window),
                                 "Error creating VkDescriptorPool");
                goto error;
        }

        pipeline->descriptor_set_layout =
                vr_calloc(sizeof(VkDescriptorSetLayout) * n_desc_sets);
        pipeline->n_desc_sets = n_desc_sets;

        const struct desc_set_bindings_info *info_p = info;

        for (unsigned i = 0; i < n_desc_sets; i++) {
                while (info_p->desc_set < i)
                        info_p++;

                VkDescriptorSetLayoutCreateInfo create_info = {
                        .sType =
                         VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
                };

                if (info_p->desc_set == i) {
                        create_info.bindingCount = info_p->n_bindings;
                        create_info.pBindings = info_p->bindings;
                }

                res = vkfn->vkCreateDescriptorSetLayout(
                                device,
                                &create_info,
                                NULL, /* allocator */
                                &pipeline->descriptor_set_layout[i]);

                if (res != VK_SUCCESS) {
                        vr_error_message(vr_window_get_config(pipeline->window),
                                         "Error creating descriptor set "
                                         "layout");
                        goto error;
                }
        }

        ret = true;

error:
        vr_free(bindings);
        vr_free(info);
        return ret;
}

static void
free_shader_code(struct vr_script_shader_code *shaders,
                 size_t n_shaders)
{
        for (unsigned i = 0; i < n_shaders; i++)
                free(shaders[i].source);
        vr_free(shaders);
}

struct vr_pipeline *
vr_pipeline_create(const struct vr_config *config,
                   struct vr_window *window,
                   const struct vr_script *script)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkResult res;
        struct vr_pipeline *pipeline = vr_calloc(sizeof *pipeline);

        pipeline->window = window;

        size_t n_shaders = vr_script_get_num_shaders(script);
        struct vr_script_shader_code *shaders =
                vr_calloc(sizeof *shaders * n_shaders);
        vr_script_get_shaders(script, NULL /* source */, shaders);

        VkShaderStageFlags script_stages = 0;

        for (unsigned i = 0; i < n_shaders; i++) {
                const struct vr_script_shader_code *shader = shaders + i;
                VkShaderStageFlags shader_bit =
                        VK_SHADER_STAGE_VERTEX_BIT << shader->stage;

                if ((script_stages & shader_bit))
                        continue;

                script_stages |= shader_bit;

                pipeline->modules[shader->stage] =
                        vr_compiler_build_stage(config,
                                                vr_window_get_context(window),
                                                script,
                                                shader->stage);
                if (pipeline->modules[shader->stage] == VK_NULL_HANDLE)
                        goto error;
        }

        VkPipelineCacheCreateInfo pipeline_cache_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
        };
        res = vkfn->vkCreatePipelineCache(vr_window_get_device(window),
                                          &pipeline_cache_create_info,
                                          NULL, /* allocator */
                                          &pipeline->pipeline_cache);
        if (res != VK_SUCCESS) {
                vr_error_message(config, "Error creating pipeline cache");
                goto error;
        }

        pipeline->stages = script_stages;

        const struct vr_script_buffer *buffers;
        size_t n_buffers;

        vr_script_get_buffers(script, &buffers, &n_buffers);

        if (n_buffers > 0) {
                if (!create_vk_descriptor_set_layout(pipeline, script))
                        goto error;
        }

        pipeline->layout = create_vk_layout(pipeline, script);
        if (pipeline->layout == VK_NULL_HANDLE)
                goto error;

        size_t n_keys;

        n_keys = vr_script_get_n_pipeline_keys(script);

        pipeline->n_pipelines = n_keys;
        pipeline->pipelines = vr_calloc(sizeof (VkPipeline) *
                                        MAX(1, pipeline->n_pipelines));

        VkPipeline first_graphics_pipeline = VK_NULL_HANDLE;

        for (int i = 0; i < pipeline->n_pipelines; i++) {
                const struct vr_pipeline_key *key =
                        vr_script_get_pipeline_key(script, i);
                switch (vr_pipeline_key_get_type(key)) {
                case VR_PIPELINE_KEY_TYPE_GRAPHICS: {
                        bool allow_derivatives = (pipeline->n_pipelines > 1 &&
                                                  first_graphics_pipeline ==
                                                  VK_NULL_HANDLE);
                        pipeline->pipelines[i] =
                                create_vk_pipeline(pipeline,
                                                   script,
                                                   key,
                                                   allow_derivatives,
                                                   first_graphics_pipeline);
                        if (first_graphics_pipeline == VK_NULL_HANDLE) {
                                first_graphics_pipeline =
                                        pipeline->pipelines[i];
                        }
                        break;
                }
                case VR_PIPELINE_KEY_TYPE_COMPUTE:
                        pipeline->pipelines[i] =
                                create_compute_pipeline(pipeline, key);
                        break;
                }

                if (pipeline->pipelines[i] == VK_NULL_HANDLE)
                        goto error;
        }

        free_shader_code(shaders, n_shaders);

        return pipeline;

error:
        free_shader_code(shaders, n_shaders);

        vr_pipeline_free(pipeline);

        return NULL;
}

size_t
vr_pipeline_get_n_desc_sets(const struct vr_pipeline *pipeline)
{
        return pipeline->n_desc_sets;
}

VkShaderStageFlagBits
vr_pipeline_get_stages(const struct vr_pipeline *pipeline)
{
        return pipeline->stages;
}

VkPipelineLayout
vr_pipeline_get_layout(const struct vr_pipeline *pipeline)
{
        return pipeline->layout;
}

const VkPipeline *
vr_pipeline_get_pipelines(const struct vr_pipeline *pipeline)
{
        return pipeline->pipelines;
}

size_t
vr_pipeline_get_n_pipelines(const struct vr_pipeline *pipeline)
{
        return pipeline->n_pipelines;
}

VkDescriptorPool
vr_pipeline_get_descriptor_pool(const struct vr_pipeline *pipeline)
{
        return pipeline->descriptor_pool;
}

const VkDescriptorSetLayout *
vr_pipeline_get_descriptor_set_layouts(const struct vr_pipeline *pipeline)
{
        return pipeline->descriptor_set_layout;
}

void
vr_pipeline_free(struct vr_pipeline *pipeline)
{
        struct vr_window *window = pipeline->window;
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkDevice device = vr_window_get_device(window);

        for (int i = 0; i < pipeline->n_pipelines; i++) {
                if (pipeline->pipelines[i]) {
                        vkfn->vkDestroyPipeline(device,
                                                pipeline->pipelines[i],
                                                NULL /* allocator */);
                }
        }
        vr_free(pipeline->pipelines);

        if (pipeline->pipeline_cache) {
                vkfn->vkDestroyPipelineCache(device,
                                             pipeline->pipeline_cache,
                                             NULL /* allocator */);
        }

        if (pipeline->layout) {
                vkfn->vkDestroyPipelineLayout(device,
                                              pipeline->layout,
                                              NULL /* allocator */);
        }

        if (pipeline->descriptor_set_layout) {
                for (unsigned i = 0; i < pipeline->n_desc_sets; i++) {
                        VkDescriptorSetLayout dsl =
                                pipeline->descriptor_set_layout[i];
                        if (dsl != VK_NULL_HANDLE) {
                                vkfn->vkDestroyDescriptorSetLayout(
                                                device,
                                                dsl,
                                                NULL /* allocator */);
                        }
                }
                vr_free(pipeline->descriptor_set_layout);
        }

        if (pipeline->descriptor_pool) {
                vkfn->vkDestroyDescriptorPool(device,
                                              pipeline->descriptor_pool,
                                              NULL /* allocator */);
        }

        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++) {
                if (pipeline->modules[i] == VK_NULL_HANDLE)
                        continue;
                vkfn->vkDestroyShaderModule(device,
                                            pipeline->modules[i],
                                            NULL /* allocator */);
        }

        vr_free(pipeline);
}
