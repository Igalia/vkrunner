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
#include "vr-subprocess.h"
#include "vr-util.h"
#include "vr-script-private.h"
#include "vr-error-message.h"
#include "vr-buffer.h"
#include "vr-temp-file.h"
#include "vr-format-private.h"

#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <limits.h>
#ifndef WIN32
#include <unistd.h>
#endif

struct desc_set_bindings_info {
        VkDescriptorSetLayoutBinding *bindings;
        unsigned n_bindings;
        unsigned desc_set;
};

static const char *
stage_names[VR_SHADER_STAGE_N_STAGES] = {
        [VR_SHADER_STAGE_VERTEX] = "vert",
        [VR_SHADER_STAGE_TESS_CTRL] = "tesc",
        [VR_SHADER_STAGE_TESS_EVAL] = "tese",
        [VR_SHADER_STAGE_GEOMETRY] = "geom",
        [VR_SHADER_STAGE_FRAGMENT] = "frag",
        [VR_SHADER_STAGE_COMPUTE] = "comp",
};

static const VkPipelineMultisampleStateCreateInfo
base_multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
};

static char *
create_file_for_shader(const struct vr_config *config,
                       const struct vr_script_shader *shader)
{
        char *filename;
        FILE *out;

        if (!vr_temp_file_create_named(config, &out, &filename))
                return NULL;

        fwrite(shader->source, 1, shader->length, out);

        fclose(out);

        return filename;
}

static bool
load_stream_contents(const struct vr_config *config,
                     FILE *stream,
                     uint8_t **contents_out,
                     size_t *size_out)
{
        size_t got;
        long pos;

        fseek(stream, 0, SEEK_END);
        pos = ftell(stream);

        if (pos == -1) {
                vr_error_message(config, "ftell failed");
                return false;
        }

        size_t size = pos;
        rewind(stream);
        uint8_t *contents = vr_alloc(size);

        got = fread(contents, 1, size, stream);
        if (got != size) {
                vr_error_message(config, "Error reading file contents");
                vr_free(contents);
                return false;
        }

        *contents_out = contents;
        *size_out = size;

        return true;
}

static bool
show_disassembly(const struct vr_config *config,
                 const char *filename)
{
        char *args[] = {
                getenv("PIGLIT_SPIRV_DIS_BINARY"),
                (char *) filename,
                NULL
        };

        if (args[0] == NULL)
                args[0] = "spirv-dis";

        return vr_subprocess_command(config, args);
}

static VkShaderModule
compile_stage(const struct vr_config *config,
              struct vr_window *window,
              const struct vr_script *script,
              enum vr_shader_stage stage)
{
        struct vr_vk_device *vkfn = window->vkdev;
        const int n_base_args = 8;
        int n_shaders = vr_list_length(&script->stages[stage]);
        char **args = alloca((n_base_args + n_shaders + 1) * sizeof args[0]);
        const struct vr_script_shader *shader;
        VkShaderModule module = VK_NULL_HANDLE;
        FILE *module_stream = NULL;
        char *module_filename;
        uint8_t *module_binary = NULL;
        size_t module_size;
        bool res;
        int i;
        char version_str[64];
        uint32_t version = vr_requirements_get_version(script->requirements);

        sprintf(version_str, "vulkan%u.%u", VK_VERSION_MAJOR(version),
                VK_VERSION_MINOR(version));

        memset(args + n_base_args, 0, (n_shaders + 1) * sizeof args[0]);

        if (!vr_temp_file_create_named(config,
                                       &module_stream,
                                       &module_filename))
                goto out;

        args[0] = getenv("PIGLIT_GLSLANG_VALIDATOR_BINARY");
        if (args[0] == NULL)
                args[0] = "glslangValidator";

        args[1] = "-V";
        args[2] = "--target-env";
        args[3] = version_str;
        args[4] = "-S";
        args[5] = (char *) stage_names[stage];
        args[6] = "-o";
        args[7] = module_filename;

        i = n_base_args;
        vr_list_for_each(shader, &script->stages[stage], link) {
                args[i] = create_file_for_shader(config, shader);
                if (args[i] == 0)
                        goto out;
                i++;
        }

        res = vr_subprocess_command(config, args);
        if (!res) {
                vr_error_message(config, "glslangValidator failed");
                goto out;
        }

        if (config->show_disassembly)
                show_disassembly(config, module_filename);

        if (!load_stream_contents(config,
                                  module_stream,
                                  &module_binary,
                                  &module_size))
                goto out;

        VkShaderModuleCreateInfo shader_module_create_info = {
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = module_size,
                        .pCode = (const uint32_t *) module_binary
        };
        res = vkfn->vkCreateShaderModule(window->device,
                                         &shader_module_create_info,
                                         NULL, /* allocator */
                                         &module);
        if (res != VK_SUCCESS) {
                vr_error_message(config, "vkCreateShaderModule failed");
                module = VK_NULL_HANDLE;
                goto out;
        }

out:
        for (i = 0; i < n_shaders; i++) {
                if (args[i + n_base_args]) {
                        unlink(args[i + n_base_args]);
                        vr_free(args[i + n_base_args]);
                }
        }

        if (module_stream) {
                fclose(module_stream);
                unlink(module_filename);
                vr_free(module_filename);
        }

        if (module_binary)
                vr_free(module_binary);

        return module;
}

static VkShaderModule
assemble_stage(const struct vr_config *config,
               struct vr_window *window,
               const struct vr_script *script,
               const struct vr_script_shader *shader)
{
        struct vr_vk_device *vkfn = window->vkdev;
        FILE *module_stream = NULL;
        char *module_filename;
        char *source_filename = NULL;
        uint8_t *module_binary = NULL;
        VkShaderModule module = VK_NULL_HANDLE;
        size_t module_size;
        bool res;
        char version_str[64];
        uint32_t version = vr_requirements_get_version(script->requirements);

        sprintf(version_str, "vulkan%u.%u", VK_VERSION_MAJOR(version),
                VK_VERSION_MINOR(version));

        if (!vr_temp_file_create_named(config,
                                       &module_stream,
                                       &module_filename))
                goto out;

        source_filename = create_file_for_shader(config, shader);
        if (source_filename == NULL)
                goto out;

        char *args[] = {
                getenv("PIGLIT_SPIRV_AS_BINARY"),
                "--target-env", version_str,
                "-o", module_filename,
                source_filename,
                NULL
        };

        if (args[0] == NULL)
                args[0] = "spirv-as";

        res = vr_subprocess_command(config, args);
        if (!res) {
                vr_error_message(config, "spirv-as failed");
                goto out;
        }

        if (config->show_disassembly)
                show_disassembly(config, module_filename);

        if (!load_stream_contents(config,
                                  module_stream,
                                  &module_binary,
                                  &module_size))
                goto out;

        VkShaderModuleCreateInfo shader_module_create_info = {
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = module_size,
                        .pCode = (const uint32_t *) module_binary
        };
        res = vkfn->vkCreateShaderModule(window->device,
                                         &shader_module_create_info,
                                         NULL, /* allocator */
                                         &module);
        if (res != VK_SUCCESS) {
                vr_error_message(config, "vkCreateShaderModule failed");
                module = VK_NULL_HANDLE;
                goto out;
        }

out:
        if (source_filename) {
                unlink(source_filename);
                vr_free(source_filename);
        }

        if (module_stream) {
                fclose(module_stream);
                unlink(module_filename);
                vr_free(module_filename);
        }

        if (module_binary)
                vr_free(module_binary);

        return module;
}

static VkShaderModule
load_binary_stage(const struct vr_config *config,
                  struct vr_window *window,
                  const struct vr_script_shader *shader)
{
        struct vr_vk_device *vkfn = window->vkdev;
        VkShaderModule module = VK_NULL_HANDLE;
        bool res;

        if (config->show_disassembly) {
                FILE *module_stream;
                char *module_filename;

                if (vr_temp_file_create_named(config,
                                              &module_stream,
                                              &module_filename)) {
                        fwrite(shader->source,
                               1, shader->length,
                               module_stream);
                        fclose(module_stream);

                        show_disassembly(config, module_filename);

                        unlink(module_filename);
                        vr_free(module_filename);
                }
        }

        VkShaderModuleCreateInfo shader_module_create_info = {
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = shader->length,
                        .pCode = (const uint32_t *) shader->source
        };
        res = vkfn->vkCreateShaderModule(window->device,
                                         &shader_module_create_info,
                                         NULL, /* allocator */
                                         &module);
        if (res != VK_SUCCESS)
                vr_error_message(config, "vkCreateShaderModule failed");

        return module;
}

static VkShaderModule
build_stage(const struct vr_config *config,
            struct vr_window *window,
            const struct vr_script *script,
            enum vr_shader_stage stage)
{
        assert(!vr_list_empty(&script->stages[stage]));

        struct vr_script_shader *shader =
                vr_container_of(script->stages[stage].next,
                                struct vr_script_shader,
                                link);

        switch (shader->source_type) {
        case VR_SCRIPT_SOURCE_TYPE_GLSL:
                return compile_stage(config, window, script, stage);
        case VR_SCRIPT_SOURCE_TYPE_SPIRV:
                return assemble_stage(config, window, script, shader);
        case VR_SCRIPT_SOURCE_TYPE_BINARY:
                return load_binary_stage(config, window, shader);
        }

        vr_fatal("should not be reached");
}

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

        if (key_source == VR_PIPELINE_KEY_SOURCE_VERTEX_DATA &&
            script->vertex_data == NULL)
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

        int n_attribs = vr_vbo_get_num_attribs(script->vertex_data);
        VkVertexInputAttributeDescription *attrib_desc =
                vr_calloc((sizeof *attrib_desc) * n_attribs);

        state->vertexAttributeDescriptionCount = n_attribs;
        state->pVertexAttributeDescriptions = attrib_desc;
        input_binding[0].stride = vr_vbo_get_stride(script->vertex_data);

        vr_vbo_for_each_attrib(script->vertex_data,
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
        struct vr_vk_device *vkfn = window->vkdev;
        VkResult res;
        int num_stages = 0;

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

        VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };

        VkViewport viewports[] = {
                {
                        .width = window->format.width,
                        .height = window->format.height,
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f
                }
        };
        VkRect2D scissors[] = {
                {
                        .extent = { window->format.width,
                                    window->format.height }
                }
        };
        VkPipelineViewportStateCreateInfo viewport_state = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = VR_N_ELEMENTS(viewports),
                .pViewports = viewports,
                .scissorCount = VR_N_ELEMENTS(scissors),
                .pScissors = scissors
        };

        VkPipelineRasterizationStateCreateInfo rasterization_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };

        VkPipelineVertexInputStateCreateInfo vertex_input_state;
        set_vertex_input_state(script, &vertex_input_state, key);

        VkPipelineTessellationStateCreateInfo tessellation_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO
        };

        VkPipelineColorBlendAttachmentState blend_attachments[] = {
                {
                        .blendEnable = false,
                }
        };

        VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        };

        VkPipelineColorBlendStateCreateInfo color_blend_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = VR_N_ELEMENTS(blend_attachments),
                .pAttachments = blend_attachments
        };

        VkGraphicsPipelineCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pViewportState = &viewport_state,
                .pRasterizationState = &rasterization_state,
                .pMultisampleState = &base_multisample_state,
                .pDepthStencilState = &depth_stencil_state,
                .pColorBlendState = &color_blend_state,
                .pTessellationState = &tessellation_state,
                .subpass = 0,
                .basePipelineHandle = parent_pipeline,
                .basePipelineIndex = -1,

                .stageCount = num_stages,
                .pStages = stages,
                .pVertexInputState = &vertex_input_state,
                .pInputAssemblyState = &input_assembly_state,
                .layout = pipeline->layout,
                .renderPass = window->render_pass[0],
        };

        vr_pipeline_key_to_create_info(key, &info);

        if (allow_derivatives)
                info.flags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
        if (parent_pipeline)
                info.flags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;

        if (!(pipeline->stages & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                  VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)))
                info.pTessellationState = NULL;

        VkPipeline vk_pipeline;

        res = vkfn->vkCreateGraphicsPipelines(window->device,
                                              pipeline->pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              &vk_pipeline);

        vr_free((void *) vertex_input_state.pVertexBindingDescriptions);
        vr_free((void *) vertex_input_state.pVertexAttributeDescriptions);

        if (res != VK_SUCCESS) {
                vr_error_message(window->config, "Error creating VkPipeline");
                return VK_NULL_HANDLE;
        }

        return vk_pipeline;
}

static VkPipeline
create_compute_pipeline(struct vr_pipeline *pipeline,
                        const struct vr_pipeline_key *key)
{
        struct vr_window *window = pipeline->window;
        struct vr_vk_device *vkfn = window->vkdev;
        const char *entrypoint =
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

        res = vkfn->vkCreateComputePipelines(window->device,
                                             pipeline->pipeline_cache,
                                             1, /* nCreateInfos */
                                             &info,
                                             NULL, /* allocator */
                                             &vk_pipeline);

        if (res != VK_SUCCESS) {
                vr_error_message(window->config, "Error creating VkPipeline");
                return VK_NULL_HANDLE;
        }

        return vk_pipeline;
}

static size_t
get_push_constant_size(const struct vr_script *script)
{
        size_t max = 0;

        for (int i = 0; i < script->n_commands; i++) {
                const struct vr_script_command *command =
                        script->commands + i;
                if (command->op != VR_SCRIPT_OP_SET_PUSH_CONSTANT)
                        continue;

                size_t end = (command->set_push_constant.offset +
                              command->set_push_constant.size);

                if (end > max)
                        max = end;
        }

        return max;
}

static VkShaderStageFlags
get_script_stages(const struct vr_script *script)
{
        VkShaderStageFlags flags = 0;

        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++) {
                if (!vr_list_empty(script->stages + i))
                        flags |= VK_SHADER_STAGE_VERTEX_BIT << i;
        }

        return flags;
}

static VkPipelineLayout
create_vk_layout(struct vr_pipeline *pipeline,
                 const struct vr_script *script)
{
        struct vr_vk_device *vkfn = pipeline->window->vkdev;
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

        VkPipelineLayout layout;
        res = vkfn->vkCreatePipelineLayout(pipeline->window->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           &layout);
        if (res != VK_SUCCESS) {
                vr_error_message(pipeline->window->config,
                                 "Error creating pipeline layout");
                return VK_NULL_HANDLE;
        }

        return layout;
}

static bool
create_vk_descriptor_set_layout(struct vr_pipeline *pipeline,
                                const struct vr_script *script)
{
        struct vr_vk_device *vkfn = pipeline->window->vkdev;
        VkResult res;
        bool ret = false;
        size_t n_buffers = script->n_buffers;

        assert(n_buffers);

        VkDescriptorSetLayoutBinding *bindings =
                vr_calloc(sizeof (*bindings) * n_buffers);
        struct desc_set_bindings_info *info =
                vr_alloc(sizeof *info * n_buffers);
        size_t n_used_desc_sets = 0;
        unsigned prev_desc_set = UINT_MAX;

        unsigned n_ubo = 0;
        unsigned n_ssbo = 0;

        for (unsigned i = 0; i < n_buffers; i++) {
                const struct vr_script_buffer *buffer = script->buffers + i;
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

        res = vkfn->vkCreateDescriptorPool(pipeline->window->device,
                                           &descriptor_pool_create_info,
                                           NULL, /* allocator */
                                           &pipeline->descriptor_pool);
        if (res != VK_SUCCESS) {
                vr_error_message(pipeline->window->config,
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
                                pipeline->window->device,
                                &create_info,
                                NULL, /* allocator */
                                &pipeline->descriptor_set_layout[i]);

                if (res != VK_SUCCESS) {
                        vr_error_message(pipeline->window->config,
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

struct vr_pipeline *
vr_pipeline_create(const struct vr_config *config,
                   struct vr_window *window,
                   const struct vr_script *script)
{
        struct vr_vk_device *vkfn = window->vkdev;
        VkResult res;
        struct vr_pipeline *pipeline = vr_calloc(sizeof *pipeline);

        pipeline->window = window;

        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++) {
                if (vr_list_empty(&script->stages[i]))
                        continue;

                pipeline->modules[i] = build_stage(config, window, script, i);
                if (pipeline->modules[i] == VK_NULL_HANDLE)
                        goto error;
        }

        VkPipelineCacheCreateInfo pipeline_cache_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
        };
        res = vkfn->vkCreatePipelineCache(window->device,
                                          &pipeline_cache_create_info,
                                          NULL, /* allocator */
                                          &pipeline->pipeline_cache);
        if (res != VK_SUCCESS) {
                vr_error_message(config, "Error creating pipeline cache");
                goto error;
        }

        pipeline->stages = get_script_stages(script);

        if (script->n_buffers > 0) {
                if (!create_vk_descriptor_set_layout(pipeline, script))
                        goto error;
        }

        pipeline->layout = create_vk_layout(pipeline, script);
        if (pipeline->layout == VK_NULL_HANDLE)
                goto error;

        struct vr_pipeline_key * const *keys = script->pipeline_keys;

        pipeline->n_pipelines = script->n_pipeline_keys;
        pipeline->pipelines = vr_calloc(sizeof (VkPipeline) *
                                        MAX(1, pipeline->n_pipelines));

        VkPipeline first_graphics_pipeline = VK_NULL_HANDLE;

        for (int i = 0; i < pipeline->n_pipelines; i++) {
                switch (vr_pipeline_key_get_type(keys[i])) {
                case VR_PIPELINE_KEY_TYPE_GRAPHICS: {
                        bool allow_derivatives = (pipeline->n_pipelines > 1 &&
                                                  first_graphics_pipeline ==
                                                  VK_NULL_HANDLE);
                        pipeline->pipelines[i] =
                                create_vk_pipeline(pipeline,
                                                   script,
                                                   keys[i],
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
                                create_compute_pipeline(pipeline, keys[i]);
                        break;
                }

                if (pipeline->pipelines[i] == VK_NULL_HANDLE)
                        goto error;
        }

        return pipeline;

error:
        vr_pipeline_free(pipeline);
        return NULL;
}

void
vr_pipeline_free(struct vr_pipeline *pipeline)
{
        struct vr_window *window = pipeline->window;
        struct vr_vk_device *vkfn = window->vkdev;

        for (int i = 0; i < pipeline->n_pipelines; i++) {
                if (pipeline->pipelines[i]) {
                        vkfn->vkDestroyPipeline(window->device,
                                                pipeline->pipelines[i],
                                                NULL /* allocator */);
                }
        }
        vr_free(pipeline->pipelines);

        if (pipeline->pipeline_cache) {
                vkfn->vkDestroyPipelineCache(window->device,
                                             pipeline->pipeline_cache,
                                             NULL /* allocator */);
        }

        if (pipeline->layout) {
                vkfn->vkDestroyPipelineLayout(window->device,
                                              pipeline->layout,
                                              NULL /* allocator */);
        }

        if (pipeline->descriptor_set_layout) {
                for (unsigned i = 0; i < pipeline->n_desc_sets; i++) {
                        VkDescriptorSetLayout dsl =
                                pipeline->descriptor_set_layout[i];
                        if (dsl != VK_NULL_HANDLE) {
                                vkfn->vkDestroyDescriptorSetLayout(
                                                window->device,
                                                dsl,
                                                NULL /* allocator */);
                        }
                }
                vr_free(pipeline->descriptor_set_layout);
        }

        if (pipeline->descriptor_pool) {
                vkfn->vkDestroyDescriptorPool(window->device,
                                              pipeline->descriptor_pool,
                                              NULL /* allocator */);
        }

        for (int i = 0; i < VR_SHADER_STAGE_N_STAGES; i++) {
                if (pipeline->modules[i] == VK_NULL_HANDLE)
                        continue;
                vkfn->vkDestroyShaderModule(window->device,
                                            pipeline->modules[i],
                                            NULL /* allocator */);
        }

        vr_free(pipeline);
}
