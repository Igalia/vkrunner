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
#include "vr-buffer.h"

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

static const VkPipelineMultisampleStateCreateInfo
base_multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
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

static void
set_vertex_input_state(const struct vr_script *script,
                       VkPipelineVertexInputStateCreateInfo *state,
                       const struct vr_pipeline_key *key)
{
        VkVertexInputBindingDescription *input_binding =
                vr_calloc(sizeof *input_binding);

        input_binding[0].binding = 0;
        input_binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        memset(state, 0, sizeof *state);

        state->sType =
                VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        state->vertexBindingDescriptionCount = 1;
        state->pVertexBindingDescriptions = input_binding;

        if (key->source == VR_PIPELINE_KEY_SOURCE_RECTANGLE) {
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

        int n_attribs = vr_list_length(&script->vertex_data->attribs);
        VkVertexInputAttributeDescription *attrib_desc =
                vr_calloc((sizeof *attrib_desc) * n_attribs);
        const struct vr_vbo_attrib *attrib;

        state->vertexAttributeDescriptionCount = n_attribs;
        state->pVertexAttributeDescriptions = attrib_desc;
        input_binding[0].stride = script->vertex_data->stride;

        vr_list_for_each(attrib, &script->vertex_data->attribs, link) {
                attrib_desc->location = attrib->location;
                attrib_desc->binding = 0;
                attrib_desc->format = attrib->format->vk_format,
                attrib_desc->offset = attrib->offset;
                attrib_desc++;
        };
}

static VkPipeline
create_vk_pipeline(struct vr_pipeline *pipeline,
                   const struct vr_script *script,
                   const struct vr_pipeline_key *key,
                   bool allow_derivatives,
                   VkPipeline parent_pipeline)
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

        VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
                .sType =
                VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
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
                .pViewportState = &base_viewport_state,
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

        res = vr_vk.vkCreateGraphicsPipelines(window->device,
                                              pipeline->pipeline_cache,
                                              1, /* nCreateInfos */
                                              &info,
                                              NULL, /* allocator */
                                              &vk_pipeline);

        vr_free((void *) vertex_input_state.pVertexBindingDescriptions);
        vr_free((void *) vertex_input_state.pVertexAttributeDescriptions);

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
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };

        if (push_constant_range.size > 0) {
                pipeline_layout_create_info.pushConstantRangeCount = 1;
                pipeline_layout_create_info.pPushConstantRanges =
                        &push_constant_range;
        }

        if (pipeline->descriptor_set_layout) {
                pipeline_layout_create_info.setLayoutCount = 1;
                pipeline_layout_create_info.pSetLayouts =
                        &pipeline->descriptor_set_layout;
        }

        VkPipelineLayout layout;
        res = vr_vk.vkCreatePipelineLayout(pipeline->window->device,
                                           &pipeline_layout_create_info,
                                           NULL, /* allocator */
                                           &layout);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating pipeline layout");
                return NULL;
        }

        return layout;
}

static VkDescriptorSetLayout
create_vk_descriptor_set_layout(struct vr_pipeline *pipeline,
                                const struct vr_script *script)
{
        VkResult res;
        size_t n_buffers = script->n_buffers;
        VkDescriptorSetLayoutBinding *bindings =
                vr_calloc(sizeof (*bindings) * n_buffers);

        for (unsigned i = 0; i < n_buffers; i++) {
                const struct vr_script_buffer *buffer = script->buffers + i;
                bindings[i].binding = buffer->binding;
                bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                bindings[i].descriptorCount = 1;
                bindings[i].stageFlags = pipeline->stages;
        }

        VkDescriptorSetLayoutCreateInfo create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .bindingCount = n_buffers,
                .pBindings = bindings
        };

        VkDescriptorSetLayout descriptor_set_layout;

        res = vr_vk.vkCreateDescriptorSetLayout(pipeline->window->device,
                                                &create_info,
                                                NULL, /* allocator */
                                                &descriptor_set_layout);

        vr_free(bindings);

        if (res != VK_SUCCESS) {
                vr_error_message("Error creating descriptor set layout");
                return NULL;
        }

        return descriptor_set_layout;
}

static int
find_key(size_t haystack_size,
         const struct vr_pipeline_key *haystack,
         const struct vr_pipeline_key *needle)
{
        for (int i = 0; i < haystack_size; i++) {
                if (vr_pipeline_key_equal(haystack + i, needle))
                        return i;
        }

        return -1;
}

static const struct vr_pipeline_key *
get_key_for_command(const struct vr_script_command *command)
{
        if (command->op == VR_SCRIPT_OP_DRAW_RECT) {
                return &command->draw_rect.key;
        } else if (command->op == VR_SCRIPT_OP_DRAW_ARRAYS) {
                return &command->draw_arrays.key;
        } else {
                return NULL;
        }
}

static void
get_keys(const struct vr_script *script,
         struct vr_pipeline_key **keys_out,
         int *n_keys_out)
{
        int n_keys = 0;
        struct vr_buffer buffer = VR_BUFFER_STATIC_INIT;

        for (int i = 0; i < script->n_commands; i++) {
                const struct vr_script_command *command = script->commands + i;
                const struct vr_pipeline_key *command_key;

                command_key = get_key_for_command(command);
                if (command_key == NULL)
                        continue;

                int key_index = find_key(n_keys,
                                         (struct vr_pipeline_key *) buffer.data,
                                         command_key);
                if (key_index != -1)
                        continue;

                vr_buffer_set_length(&buffer,
                                     buffer.length +
                                     sizeof (struct vr_pipeline_key));
                struct vr_pipeline_key *key =
                        (struct vr_pipeline_key *) (buffer.data +
                                                    buffer.length) - 1;
                *key = *command_key;
                n_keys++;
        }

        if (n_keys == 0) {
                /* Always create at least one pipeline */
                struct vr_pipeline_key default_key;
                vr_pipeline_key_init(&default_key);
                *keys_out = vr_memdup(&default_key, sizeof default_key);
                *n_keys_out = 1;
        } else {
                *keys_out = (struct vr_pipeline_key *) buffer.data;
                *n_keys_out = n_keys;
        }
}

VkPipeline
vr_pipeline_for_command(struct vr_pipeline *pipeline,
                        const struct vr_script_command *command)
{
        const struct vr_pipeline_key *key;

        key = get_key_for_command(command);
        assert(key != NULL);

        int key_index = find_key(pipeline->n_pipelines,
                                 pipeline->keys,
                                 key);
        assert(key_index != -1);

        return pipeline->pipelines[key_index];
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

        if (script->n_buffers > 0) {
                pipeline->descriptor_set_layout =
                        create_vk_descriptor_set_layout(pipeline, script);
                if (pipeline->descriptor_set_layout == NULL)
                        goto error;
        }

        pipeline->layout = create_vk_layout(pipeline, script);
        if (pipeline->layout == NULL)
                goto error;

        get_keys(script, &pipeline->keys, &pipeline->n_pipelines);

        pipeline->pipelines = vr_calloc(sizeof (VkPipeline) *
                                        pipeline->n_pipelines);

        bool use_derivatives = pipeline->n_pipelines > 1;

        for (int i = 0; i < pipeline->n_pipelines; i++) {
                pipeline->pipelines[i] =
                        create_vk_pipeline(pipeline,
                                           script,
                                           pipeline->keys + i,
                                           i == 0 && use_derivatives,
                                           pipeline->pipelines[0]);
                if (pipeline->pipelines[i] == NULL)
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

        for (int i = 0; i < pipeline->n_pipelines; i++) {
                if (pipeline->pipelines[i]) {
                        vr_vk.vkDestroyPipeline(window->device,
                                                pipeline->pipelines[i],
                                                NULL /* allocator */);
                }
        }
        vr_free(pipeline->pipelines);
        vr_free(pipeline->keys);

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

        if (pipeline->descriptor_set_layout) {
                VkDescriptorSetLayout dsl = pipeline->descriptor_set_layout;
                vr_vk.vkDestroyDescriptorSetLayout(window->device,
                                                   dsl,
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
