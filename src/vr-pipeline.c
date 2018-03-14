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
#include "vr-script.h"
#include "vr-error-message.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

static const char *
stage_names[VR_SCRIPT_N_STAGES] = {
        [VR_SCRIPT_SHADER_STAGE_VERTEX] = "vert",
        [VR_SCRIPT_SHADER_STAGE_TESS_CTRL] = "tesc",
        [VR_SCRIPT_SHADER_STAGE_TESS_EVAL] = "tese",
        [VR_SCRIPT_SHADER_STAGE_GEOMETRY] = "geom",
        [VR_SCRIPT_SHADER_STAGE_FRAGMENT] = "frag",
        [VR_SCRIPT_SHADER_STAGE_COMPUTE] = "comp",
};

static const VkViewport
base_viewports[] = {
        {
                .width = VR_WINDOW_WIDTH,
                .height = VR_WINDOW_HEIGHT,
                .minDepth = 0.0f,
                .maxDepth = 1.0f
        }
};

static const VkRect2D
base_scissors[] = {
        {
                .extent = { VR_WINDOW_WIDTH, VR_WINDOW_HEIGHT }
        }
};

static const VkPipelineViewportStateCreateInfo
base_viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = VR_N_ELEMENTS(base_viewports),
        .pViewports = base_viewports,
        .scissorCount = VR_N_ELEMENTS(base_scissors),
        .pScissors = base_scissors
};

static const VkPipelineRasterizationStateCreateInfo
base_rasterization_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
};

static const VkPipelineMultisampleStateCreateInfo
base_multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
};

static const VkPipelineDepthStencilStateCreateInfo
base_depth_stencil_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = false,
        .depthWriteEnable = false,
        .depthCompareOp = VK_COMPARE_OP_LESS
};

static const VkPipelineColorBlendAttachmentState base_blend_attachments[] = {
        {
                .blendEnable = false,
                .colorWriteMask = (VK_COLOR_COMPONENT_R_BIT |
                                   VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT)
        }
};

static const VkPipelineColorBlendStateCreateInfo base_color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = VR_N_ELEMENTS(base_blend_attachments),
        .pAttachments = base_blend_attachments
};

static bool
create_named_temp_file(FILE **stream_out,
                       char **filename_out)
{
        char filename[] = "/tmp/vkrunner-XXXXXX";
        int fd = mkstemp(filename);
        FILE *stream;

        if (fd == -1) {
                vr_error_message("mkstemp: %s", strerror(errno));
                return false;
        }

        stream = fdopen(fd, "r+");
        if (stream == NULL) {
                vr_error_message("%s: %s", filename, strerror(errno));
                close(fd);
                return false;
        }

        *filename_out = vr_strdup(filename);
        *stream_out = stream;

        return true;
}

static char *
create_file_for_shader(const struct vr_script_shader *shader)
{
        char *filename;
        FILE *out;

        if (!create_named_temp_file(&out, &filename))
                return NULL;

        fwrite(shader->source, 1, shader->length, out);

        fclose(out);

        return filename;
}

static bool
load_stream_contents(FILE *stream,
                     uint8_t **contents,
                     size_t *size)
{
        ssize_t got;
        long pos;

        fseek(stream, 0, SEEK_END);
        pos = ftell(stream);

        if (pos == -1) {
                vr_error_message("ftell failed");
                return false;
        }

        *size = pos;
        rewind(stream);
        *contents = vr_alloc(*size);

        got = fread(*contents, 1, *size, stream);
        if (got != *size) {
                vr_error_message("Error reading file contents");
                vr_free(contents);
                return false;
        }

        return true;
}

static bool
show_disassembly(const char *filename)
{
        char *args[] = {
                getenv("PIGLIT_SPIRV_DIS_BINARY"),
                (char *) filename,
                NULL
        };

        if (args[0] == NULL)
                args[0] = "spirv-dis";

        return vr_subprocess_command(args);
}

static VkShaderModule
compile_stage(const struct vr_config *config,
              struct vr_window *window,
              const struct vr_script *script,
              enum vr_script_shader_stage stage)
{
        const int n_base_args = 6;
        int n_shaders = vr_list_length(&script->stages[stage]);
        char **args = alloca((n_base_args + n_shaders + 1) * sizeof args[0]);
        const struct vr_script_shader *shader;
        VkShaderModule module = NULL;
        FILE *module_stream = NULL;
        char *module_filename;
        uint8_t *module_binary = NULL;
        size_t module_size;
        bool res;
        int i;

        if (!create_named_temp_file(&module_stream, &module_filename))
                goto out;

        args[0] = getenv("PIGLIT_GLSLANG_VALIDATOR_BINARY");
        if (args[0] == NULL)
                args[0] = "glslangValidator";

        args[1] = "-V";
        args[2] = "-S";
        args[3] = (char *) stage_names[stage];
        args[4] = "-o";
        args[5] = module_filename;
        memset(args + n_base_args, 0, (n_shaders + 1) * sizeof args[0]);

        i = n_base_args;
        vr_list_for_each(shader, &script->stages[stage], link) {
                args[i] = create_file_for_shader(shader);
                if (args[i] == 0)
                        goto out;
                i++;
        }

        res = vr_subprocess_command(args);
        if (!res) {
                vr_error_message("glslangValidator failed");
                goto out;
        }

        if (config->show_disassembly)
                show_disassembly(module_filename);

        if (!load_stream_contents(module_stream, &module_binary, &module_size))
                goto out;

        VkShaderModuleCreateInfo shader_module_create_info = {
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = module_size,
                        .pCode = (const uint32_t *) module_binary
        };
        res = vr_vk.vkCreateShaderModule(window->device,
                                         &shader_module_create_info,
                                         NULL, /* allocator */
                                         &module);
        if (res != VK_SUCCESS) {
                vr_error_message("vkCreateShaderModule failed");
                module = NULL;
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
               const struct vr_script_shader *shader)
{
        FILE *module_stream = NULL;
        char *module_filename;
        char *source_filename = NULL;
        uint8_t *module_binary = NULL;
        VkShaderModule module = NULL;
        size_t module_size;
        bool res;

        if (!create_named_temp_file(&module_stream, &module_filename))
                goto out;

        source_filename = create_file_for_shader(shader);
        if (source_filename == NULL)
                goto out;

        char *args[] = {
                getenv("PIGLIT_SPIRV_AS_BINARY"),
                "-o", module_filename,
                source_filename,
                NULL
        };

        if (args[0] == NULL)
                args[0] = "spirv-as";

        res = vr_subprocess_command(args);
        if (!res) {
                vr_error_message("spirv-as failed");
                goto out;
        }

        if (config->show_disassembly)
                show_disassembly(module_filename);

        if (!load_stream_contents(module_stream, &module_binary, &module_size))
                goto out;

        VkShaderModuleCreateInfo shader_module_create_info = {
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = module_size,
                        .pCode = (const uint32_t *) module_binary
        };
        res = vr_vk.vkCreateShaderModule(window->device,
                                         &shader_module_create_info,
                                         NULL, /* allocator */
                                         &module);
        if (res != VK_SUCCESS) {
                vr_error_message("vkCreateShaderModule failed");
                module = NULL;
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
build_stage(const struct vr_config *config,
            struct vr_window *window,
            const struct vr_script *script,
            enum vr_script_shader_stage stage)
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
                return assemble_stage(config, window, shader);
        }

        vr_fatal("should not be reached");
}

static VkPipeline
create_vk_pipeline(struct vr_pipeline *pipeline)
{
        struct vr_window *window = pipeline->window;
        VkResult res;
        int num_stages = 0;

        VkPipelineShaderStageCreateInfo stages[VR_SCRIPT_N_STAGES] = { };

        for (int i = 0; i < VR_SCRIPT_N_STAGES; i++) {
                if (pipeline->modules[i] == NULL)
                        continue;
                stages[num_stages].sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[num_stages].stage = VK_SHADER_STAGE_VERTEX_BIT << i;
                stages[num_stages].module = pipeline->modules[i];
                stages[num_stages].pName = "main";
                num_stages++;
        }

        VkVertexInputBindingDescription input_binding_descriptions[] = {
                {
                        .binding = 0,
                        .stride = sizeof (struct vr_pipeline_vertex),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                },
        };
        VkVertexInputAttributeDescription attribute_descriptions[] = {
                {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(struct vr_pipeline_vertex, x)
                },
        };
        VkPipelineVertexInputStateCreateInfo vertex_input_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                .vertexBindingDescriptionCount =
                VR_N_ELEMENTS(input_binding_descriptions),
                .pVertexBindingDescriptions = input_binding_descriptions,
                .vertexAttributeDescriptionCount =
                VR_N_ELEMENTS(attribute_descriptions),
                .pVertexAttributeDescriptions = attribute_descriptions
        };
        VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                .primitiveRestartEnable = false
        };

        VkGraphicsPipelineCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .pViewportState = &base_viewport_state,
                .pRasterizationState = &base_rasterization_state,
                .pMultisampleState = &base_multisample_state,
                .pDepthStencilState = &base_depth_stencil_state,
                .pColorBlendState = &base_color_blend_state,
                .subpass = 0,
                .basePipelineHandle = NULL,
                .basePipelineIndex = -1,

                .stageCount = num_stages,
                .pStages = stages,
                .pVertexInputState = &vertex_input_state,
                .pInputAssemblyState = &input_assembly_state,
                .layout = pipeline->layout,
                .renderPass = window->render_pass
        };

        VkPipeline vk_pipeline;

        res = vr_vk.vkCreateGraphicsPipelines(window->device,
                                              pipeline->pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              &vk_pipeline);

        if (res != VK_SUCCESS) {
                vr_error_message("Error creating VkPipeline");
                return NULL;
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

                enum vr_script_type type =
                        command->set_push_constant.value.type;
                size_t value_size = vr_script_type_size(type);
                size_t end = command->set_push_constant.offset + value_size;

                if (end > max)
                        max = end;
        }

        return max;
}

static VkShaderStageFlags
get_script_stages(const struct vr_script *script)
{
        VkShaderStageFlags flags = 0;

        for (int i = 0; i < VR_SCRIPT_N_STAGES; i++) {
                if (!vr_list_empty(script->stages + i))
                        flags |= VK_SHADER_STAGE_VERTEX_BIT << i;
        }

        return flags;
}

static VkPipelineLayout
create_vk_layout(struct vr_pipeline *pipeline,
                 const struct vr_script *script)
{
        VkResult res;

        VkPushConstantRange push_constant_range = {
                .stageFlags = pipeline->stages,
                .offset = 0,
                .size = get_push_constant_size(script)
        };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = 0
        };

        if (push_constant_range.size > 0) {
                pipeline_layout_create_info.pushConstantRangeCount = 1;
                pipeline_layout_create_info.pPushConstantRanges =
                        &push_constant_range;
        }

        VkPipelineLayout layout;
        res = vr_vk.vkCreatePipelineLayout(pipeline->window->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           &layout);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating empty layout");
                return NULL;
        }

        return layout;
}

struct vr_pipeline *
vr_pipeline_create(const struct vr_config *config,
                   struct vr_window *window,
                   const struct vr_script *script)
{
        VkResult res;
        struct vr_pipeline *pipeline = vr_calloc(sizeof *pipeline);

        pipeline->window = window;

        for (int i = 0; i < VR_SCRIPT_N_STAGES; i++) {
                if (vr_list_empty(&script->stages[i]))
                        continue;

                pipeline->modules[i] = build_stage(config, window, script, i);
                if (pipeline->modules[i] == NULL)
                        goto error;
        }

        VkPipelineCacheCreateInfo pipeline_cache_create_info = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO
        };
        res = vr_vk.vkCreatePipelineCache(window->device,
                                          &pipeline_cache_create_info,
                                          NULL, /* allocator */
                                          &pipeline->pipeline_cache);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating pipeline cache");
                goto error;
        }

        pipeline->stages = get_script_stages(script);

        pipeline->layout = create_vk_layout(pipeline, script);
        if (pipeline->layout == NULL)
                goto error;

        pipeline->pipeline = create_vk_pipeline(pipeline);
        if (pipeline->pipeline == NULL)
                goto error;

        return pipeline;

error:
        vr_pipeline_free(pipeline);
        return NULL;
}

void
vr_pipeline_free(struct vr_pipeline *pipeline)
{
        struct vr_window *window = pipeline->window;

        if (pipeline->pipeline) {
                vr_vk.vkDestroyPipeline(window->device,
                                        pipeline->pipeline,
                                        NULL /* allocator */);
        }

        if (pipeline->pipeline_cache) {
                vr_vk.vkDestroyPipelineCache(window->device,
                                             pipeline->pipeline_cache,
                                             NULL /* allocator */);
        }

        if (pipeline->layout) {
                vr_vk.vkDestroyPipelineLayout(window->device,
                                              pipeline->layout,
                                              NULL /* allocator */);
        }

        for (int i = 0; i < VR_SCRIPT_N_STAGES; i++) {
                if (pipeline->modules[i] == NULL)
                        continue;
                vr_vk.vkDestroyShaderModule(window->device,
                                            pipeline->modules[i],
                                            NULL /* allocator */);
        }

        vr_free(pipeline);
}
