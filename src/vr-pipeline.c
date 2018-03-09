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

static const char *
stage_names[VR_SCRIPT_N_STAGES] = {
        [VR_SCRIPT_SHADER_STAGE_VERTEX] = "vert",
        [VR_SCRIPT_SHADER_STAGE_TESS_CTRL] = "tesc",
        [VR_SCRIPT_SHADER_STAGE_TESS_EVAL] = "tese",
        [VR_SCRIPT_SHADER_STAGE_GEOMETRY] = "geom",
        [VR_SCRIPT_SHADER_STAGE_FRAGMENT] = "frag",
        [VR_SCRIPT_SHADER_STAGE_COMPUTE] = "comp",
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

static VkShaderModule
compile_stage(struct vr_window *window,
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

struct vr_pipeline *
vr_pipeline_create(struct vr_window *window,
                   const struct vr_script *script)
{
        struct vr_pipeline *pipeline = vr_calloc(sizeof *pipeline);

        pipeline->window = window;

        for (int i = 0; i < VR_SCRIPT_N_STAGES; i++) {
                if (vr_list_empty(&script->stages[i]))
                        continue;

                pipeline->modules[i] = compile_stage(window, script, i);
                if (pipeline->modules[i] == NULL) {
                        vr_pipeline_free(pipeline);
                        return NULL;
                }
        }

        return pipeline;
}

void
vr_pipeline_free(struct vr_pipeline *pipeline)
{
        struct vr_window *window = pipeline->window;

        for (int i = 0; i < VR_SCRIPT_N_STAGES; i++) {
                if (pipeline->modules[i] == NULL)
                        continue;
                vr_vk.vkDestroyShaderModule(window->device,
                                            pipeline->modules[i],
                                            NULL /* allocator */);
        }

        if (pipeline->pipeline) {
                vr_vk.vkDestroyPipeline(window->device,
                                        pipeline->pipeline,
                                        NULL /* allocator */);
        }

        vr_free(pipeline);
}
