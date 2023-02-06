/*
 * vkrunner
 *
 * Copyright (C) 2018 Intel Corporation
 * Copyright 2023 Neil Roberts
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

#include "vr-compiler.h"

#include <assert.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include "vr-subprocess.h"
#include "vr-temp-file.h"
#include "vr-error-message.h"

static const char *
stage_names[VR_SHADER_STAGE_N_STAGES] = {
        [VR_SHADER_STAGE_VERTEX] = "vert",
        [VR_SHADER_STAGE_TESS_CTRL] = "tesc",
        [VR_SHADER_STAGE_TESS_EVAL] = "tese",
        [VR_SHADER_STAGE_GEOMETRY] = "geom",
        [VR_SHADER_STAGE_FRAGMENT] = "frag",
        [VR_SHADER_STAGE_COMPUTE] = "comp",
};

static char *
create_file_for_shader(const struct vr_config *config,
                       const struct vr_script_shader_code *shader)
{
        char *filename;
        FILE *out;

        if (!vr_temp_file_create_named(config, &out, &filename))
                return NULL;

        fwrite(shader->source, 1, shader->source_length, out);

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
              enum vr_shader_stage stage,
              const struct vr_script_shader_code *shaders,
              size_t total_n_shaders)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        const int n_base_args = 8;
        VkShaderModule module = VK_NULL_HANDLE;
        FILE *module_stream = NULL;
        char *module_filename;
        uint8_t *module_binary = NULL;
        size_t module_size;
        bool res;
        char version_str[64];
        const struct vr_requirements *requirements =
                vr_script_get_requirements(script);
        uint32_t version = vr_requirements_get_version(requirements);

        int n_shaders = 0;

        for (unsigned i = 0; i < total_n_shaders; i++) {
                if (shaders[i].stage == stage)
                        n_shaders++;
        }

        char **args = alloca((n_base_args + n_shaders + 1) * sizeof args[0]);

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

        int i = n_base_args;
        for (unsigned shader_num = 0;
             shader_num < total_n_shaders;
             shader_num++) {
                if (shaders[shader_num].stage != stage)
                        continue;

                args[i] = create_file_for_shader(config, shaders + shader_num);
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
        res = vkfn->vkCreateShaderModule(vr_window_get_device(window),
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
               const struct vr_script_shader_code *shader)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        FILE *module_stream = NULL;
        char *module_filename;
        char *source_filename = NULL;
        uint8_t *module_binary = NULL;
        VkShaderModule module = VK_NULL_HANDLE;
        size_t module_size;
        bool res;
        char version_str[64];
        const struct vr_requirements *requirements =
                vr_script_get_requirements(script);
        uint32_t version = vr_requirements_get_version(requirements);

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
        res = vkfn->vkCreateShaderModule(vr_window_get_device(window),
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
                  const struct vr_script_shader_code *shader)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkShaderModule module = VK_NULL_HANDLE;
        bool res;

        if (config->show_disassembly) {
                FILE *module_stream;
                char *module_filename;

                if (vr_temp_file_create_named(config,
                                              &module_stream,
                                              &module_filename)) {
                        fwrite(shader->source,
                               1, shader->source_length,
                               module_stream);
                        fclose(module_stream);

                        show_disassembly(config, module_filename);

                        unlink(module_filename);
                        vr_free(module_filename);
                }
        }

        VkShaderModuleCreateInfo shader_module_create_info = {
                        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                        .codeSize = shader->source_length,
                        .pCode = (const uint32_t *) shader->source
        };
        res = vkfn->vkCreateShaderModule(vr_window_get_device(window),
                                         &shader_module_create_info,
                                         NULL, /* allocator */
                                         &module);
        if (res != VK_SUCCESS)
                vr_error_message(config, "vkCreateShaderModule failed");

        return module;
}

VkShaderModule
vr_compiler_build_stage(const struct vr_config *config,
                        struct vr_window *window,
                        const struct vr_script *script,
                        enum vr_shader_stage stage)
{
        const struct vr_script_shader_code *first_shader = NULL;

        size_t n_shaders = vr_script_get_num_shaders(script);
        struct vr_script_shader_code *shaders =
                vr_calloc(sizeof *shaders * n_shaders);
        vr_script_get_shaders(script, NULL /* source */, shaders);

        for (unsigned i = 0; i < n_shaders; i++) {
                if (shaders[i].stage == stage) {
                        first_shader = shaders + i;
                        break;
                }
        }

        assert(first_shader != NULL);

        VkShaderModule ret;

        switch (first_shader->source_type) {
        case VR_SCRIPT_SOURCE_TYPE_GLSL:
                ret = compile_stage(config,
                                     window,
                                     script,
                                     stage,
                                     shaders,
                                     n_shaders);
                goto found_source_type;
        case VR_SCRIPT_SOURCE_TYPE_SPIRV:
                ret = assemble_stage(config, window, script, first_shader);
                goto found_source_type;
        case VR_SCRIPT_SOURCE_TYPE_BINARY:
                ret = load_binary_stage(config, window, first_shader);
                goto found_source_type;
        }

        vr_fatal("should not be reached");

found_source_type:
        for (unsigned i = 0; i < n_shaders; i++)
                free(shaders[i].source);
        vr_free(shaders);

        return ret;
}
