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
#include <string.h>

#include "vr-vk.h"
#include "vr-window.h"
#include "vr-script.h"
#include "vr-pipeline.h"
#include "vr-test.h"
#include "vr-config.h"
#include "vr-error-message.h"
#include "vr-source-private.h"

struct vr_executor {
        struct vr_config config;
        struct vr_window *window;
        struct vr_context *context;
        char **extensions;
        VkPhysicalDeviceFeatures enabled_features;

        bool use_external;

        struct {
                vr_executor_get_instance_proc_cb get_instance_proc_cb;
                void *user_data;
                VkPhysicalDevice physical_device;
                int queue_family;
                VkDevice device;
        } external;
};

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
create_external_context(struct vr_executor *executor)
{
        vr_executor_get_instance_proc_cb get_instance_proc_cb =
                executor->external.get_instance_proc_cb;

        return vr_context_new_with_device(&executor->config,
                                          get_instance_proc_cb,
                                          executor->external.user_data,
                                          executor->external.physical_device,
                                          executor->external.queue_family,
                                          executor->external.device,
                                          &executor->context);
}

static enum vr_result
process_script(struct vr_executor *executor,
               const struct vr_source *source)
{
        enum vr_result res = VR_RESULT_PASS;
        struct vr_script *script = NULL;
        struct vr_pipeline *pipeline = NULL;

        script = vr_script_load(&executor->config, source);

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
                        res = create_external_context(executor);
                        if (res != VR_RESULT_PASS)
                                goto out;
                } else {
                        res = vr_context_new(&executor->config,
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
                        vr_error_message(&executor->config,
                                         "%s: A required feature is missing",
                                         script->filename);
                        res = VR_RESULT_SKIP;
                        goto out;
                }

                if (!vr_context_check_extensions(executor->context,
                                                 script->extensions)) {
                        vr_error_message(&executor->config,
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

        pipeline = vr_pipeline_create(&executor->config,
                                      executor->window,
                                      script);

        if (pipeline == NULL) {
                res = VR_RESULT_FAIL;
                goto out;
        }

        if (!vr_test_run(executor->window, pipeline, script))
                res = VR_RESULT_FAIL;

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
                       vr_executor_get_instance_proc_cb get_instance_proc_cb,
                       void *user_data,
                       /* VkPhysicalDevice */
                       void *physical_device,
                       int queue_family,
                       /* VkDevice */
                       void *device)
{
        free_context(executor);

        executor->external.get_instance_proc_cb = get_instance_proc_cb;
        executor->external.user_data = user_data;
        executor->external.physical_device = physical_device;
        executor->external.queue_family = queue_family;
        executor->external.device = device;
        executor->use_external = true;
}

void
vr_executor_set_show_disassembly(struct vr_executor *executor,
                                 bool show_disassembly)
{
        executor->config.show_disassembly = show_disassembly;
}

void
vr_executor_set_user_data(struct vr_executor *executor,
                          void *user_data)
{
        executor->config.user_data = user_data;
}

void
vr_executor_set_error_cb(struct vr_executor *executor,
                         vr_callback_error error_cb)
{
        executor->config.error_cb = error_cb;
}

void
vr_executor_set_inspect_cb(struct vr_executor *executor,
                           vr_callback_inspect inspect_cb)
{
        executor->config.inspect_cb = inspect_cb;
}

void
vr_executor_set_before_test_cb(struct vr_executor *executor,
                               vr_callback_before_test before_test_cb)
{
        executor->config.before_test_cb = before_test_cb;
}

void
vr_executor_set_after_test_cb(struct vr_executor *executor,
                              vr_callback_after_test after_test_cb)
{
        executor->config.after_test_cb = after_test_cb;
}

enum vr_result
vr_executor_execute(struct vr_executor *executor,
                    const struct vr_source *source)
{
        const char *filename;

        if (source->type == VR_SOURCE_TYPE_FILE)
                filename = source->string;
        else
                filename = NULL;

        if (executor->config.before_test_cb) {
                executor->config.before_test_cb(filename,
                                                executor->config.user_data);
        }

        enum vr_result res = process_script(executor, source);

        if (executor->config.after_test_cb) {
                executor->config.after_test_cb(filename,
                                               res,
                                               executor->config.user_data);
        }

        return res;
}

void
vr_executor_free(struct vr_executor *executor)
{
        free_context(executor);

        vr_free(executor);
}
