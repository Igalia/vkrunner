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

#include "vr-executor.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "vr-vk.h"
#include "vr-window.h"
#include "vr-script.h"
#include "vr-pipeline.h"
#include "vr-test.h"
#include "vr-config-private.h"
#include "vr-error-message.h"

struct vr_executor {
        struct vr_window *window;
        struct vr_context *context;
        char **extensions;
        VkPhysicalDeviceFeatures enabled_features;

        bool use_external;

        struct {
                void *lib_vulkan;
                VkInstance instance;
                VkPhysicalDevice physical_device;
                int queue_family;
                VkDevice device;
        } external;
};

static bool
write_ppm(struct vr_window *window,
          const char *filename)
{
        const struct vr_format *format = window->framebuffer_format;
        int format_size = vr_format_get_size(format);
        FILE *out = fopen(filename, "w");

        if (out == NULL) {
                vr_error_message(window->config,
                                 "%s: %s",
                                 filename,
                                 strerror(errno));
                return false;
        }

        fprintf(out,
                "P6\n"
                "%i %i\n"
                "255\n",
                VR_WINDOW_WIDTH,
                VR_WINDOW_HEIGHT);

        for (int y = 0; y < VR_WINDOW_HEIGHT; y++) {
                const uint8_t *p = ((uint8_t *) window->linear_memory_map +
                                    y * window->linear_memory_stride);

                for (int x = 0; x < VR_WINDOW_WIDTH; x++) {
                        double pixel[4];

                        vr_format_load_pixel(format, p, pixel);

                        for (int i = 0; i < 3; i++) {
                                double v = pixel[i];

                                if (v < 0.0)
                                        v = 0.0;
                                else if (v > 1.0)
                                        v = 1.0;

                                fputc(round(v * 255.0), out);
                        }
                        p += format_size;
                }
        }

        fclose(out);

        return true;
}

static void
free_window(struct vr_executor *executor)
{
        if (executor->window) {
                vr_window_free(executor->window);
                executor->window = NULL;
        }
}

static void
free_context(struct vr_executor *executor)
{
        if (executor->context == NULL)
                return;

        free_window(executor);

        vr_context_free(executor->context);
        executor->context = NULL;

        if (!executor->use_external) {
                for (char **ext = executor->extensions; *ext; ext++)
                        vr_free(*ext);

                vr_free(executor->extensions);
        }
}

static bool
context_is_compatible(struct vr_executor *executor,
                      const struct vr_script *script)
{
        /* If device is created externally then it’s up to the caller
         * to ensure the device has all the necessary features
         * enabled. */
        if (executor->context->device_is_external)
                return true;

        if (memcmp(&executor->enabled_features,
                   &script->required_features,
                   sizeof script->required_features))
                return false;

        const char *const *a;
        char **b;

        for (a = script->extensions, b = executor->extensions;
             true;
             a++, b++) {
                if (*a == NULL)
                        return *b == NULL;

                if (*b == NULL)
                        return false;

                if (strcmp(*a, *b))
                        return false;
        }

        return true;
}

static bool
window_is_compatible(struct vr_executor *executor,
                     const struct vr_script *script)
{
        struct vr_window *window = executor->window;

        return (script->framebuffer_format == window->framebuffer_format &&
                script->depth_stencil_format == window->depth_stencil_format);
}

static void
copy_extensions(struct vr_executor *executor,
                const char * const *extensions)
{
        int n_extensions = 0;

        for (const char * const *ext = extensions; *ext; ext++)
                n_extensions++;

        executor->extensions = vr_alloc((sizeof executor->extensions[0]) *
                                        (n_extensions + 1));

        for (int i = 0; i < n_extensions; i++)
                executor->extensions[i] = vr_strdup(extensions[i]);

        executor->extensions[n_extensions] = NULL;
}

static enum vr_result
create_external_context(struct vr_executor *executor,
                        const struct vr_config *config)
{
        return vr_context_new_with_device(config,
                                          executor->external.lib_vulkan,
                                          executor->external.instance,
                                          executor->external.physical_device,
                                          executor->external.queue_family,
                                          executor->external.device,
                                          &executor->context);
}

static enum vr_result
process_script(struct vr_executor *executor,
               const struct vr_config *config,
               const char *filename,
               const char *string)
{
        enum vr_result res = VR_RESULT_PASS;
        struct vr_script *script = NULL;
        struct vr_pipeline *pipeline = NULL;

        if (string)
                script = vr_script_load_from_string(config, string);
        else
                script = vr_script_load_from_file(config, filename);

        if (script == NULL) {
                res = VR_RESULT_FAIL;
                goto out;
        }

        /* Recreate the context if the required features or extensions
         * have changed */
        if (executor->context && !context_is_compatible(executor, script))
                free_context(executor);

        /* Recreate the window if the framebuffer format is different */
        if (executor->window && !window_is_compatible(executor, script))
                free_window(executor);

        if (executor->context == NULL) {
                if (executor->use_external) {
                        res = create_external_context(executor, config);
                        if (res != VR_RESULT_PASS)
                                goto out;
                } else {
                        res = vr_context_new(config,
                                             &script->required_features,
                                             script->extensions,
                                             &executor->context);

                        if (res != VR_RESULT_PASS)
                                goto out;

                        copy_extensions(executor, script->extensions);
                        executor->enabled_features = script->required_features;
                }
        }

        if (executor->use_external) {
                if (!vr_context_check_features(executor->context,
                                               &script->required_features)) {
                        vr_error_message(config,
                                         "%s: A required feature is missing",
                                         script->filename);
                        res = VR_RESULT_SKIP;
                        goto out;
                }

                if (!vr_context_check_extensions(executor->context,
                                                 script->extensions)) {
                        vr_error_message(config,
                                         "%s: A required extension is missing",
                                         script->filename);
                        res = VR_RESULT_SKIP;
                        goto out;
                }
        }

        if (executor->window == NULL) {
                res = vr_window_new(executor->context,
                                    script->framebuffer_format,
                                    script->depth_stencil_format,
                                    &executor->window);
                if (res != VR_RESULT_PASS)
                        goto out;
        }

        pipeline = vr_pipeline_create(config, executor->window, script);

        if (pipeline == NULL) {
                res = VR_RESULT_FAIL;
                goto out;
        }

        if (!vr_test_run(executor->window, pipeline, script))
                res = VR_RESULT_FAIL;

        if (config->image_filename) {
                if (!write_ppm(executor->window, config->image_filename))
                        res = VR_RESULT_FAIL;
        }

out:
        if (pipeline)
                vr_pipeline_free(pipeline);
        if (script)
                vr_script_free(script);

        return res;
}

struct vr_executor *
vr_executor_new(void)
{
        struct vr_executor *executor = vr_calloc(sizeof *executor);

        return executor;
}

void
vr_executor_set_device(struct vr_executor *executor,
                       void *lib_vulkan,
                       /* VkInstance */
                       void *instance,
                       /* VkPhysicalDevice */
                       void *physical_device,
                       int queue_family,
                       /* VkDevice */
                       void *device)
{
        free_context(executor);

        executor->external.lib_vulkan = lib_vulkan;
        executor->external.instance = instance;
        executor->external.physical_device = physical_device;
        executor->external.queue_family = queue_family;
        executor->external.device = device;
        executor->use_external = true;
}

enum vr_result
vr_executor_execute(struct vr_executor *executor,
                    const struct vr_config *config)
{
        enum vr_result overall_result = VR_RESULT_SKIP;

        const struct vr_config_script *script;
        vr_list_for_each(script, &config->scripts, link) {
                if (config->before_test_cb) {
                        config->before_test_cb(script->filename,
                                               config->user_data);
                }

                enum vr_result res =
                        process_script(executor,
                                       config,
                                       script->filename,
                                       script->string);

                if (config->after_test_cb) {
                        config->after_test_cb(script->filename,
                                              res,
                                              config->user_data);
                }

                overall_result = vr_result_merge(res, overall_result);
        }

        return overall_result;
}

void
vr_executor_free(struct vr_executor *executor)
{
        free_context(executor);

        vr_free(executor);
}
