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

#include "vr-config.h"
#include "vr-executor.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "vr-vk.h"
#include "vr-window.h"
#include "vr-script-private.h"
#include "vr-pipeline.h"
#include "vr-test.h"
#include "vr-error-message.h"
#include "vr-source-private.h"
#include "vr-requirements.h"

struct vr_executor {
        struct vr_config *config;
        struct vr_window *window;
        struct vr_context *context;
        struct vr_requirements *requirements;

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

        if (!executor->use_external)
                vr_requirements_free(executor->requirements);
}

static bool
context_is_compatible(struct vr_executor *executor,
                      const struct vr_script *script)
{
        /* If device is created externally then it’s up to the caller
         * to ensure the device has all the necessary features
         * enabled. */
        if (vr_context_device_is_external(executor->context))
                return true;

        const struct vr_requirements *script_requirements =
                vr_script_get_requirements(script);

        if (!vr_requirements_equal(executor->requirements,
                                   script_requirements))
                return false;

        return true;
}

static enum vr_result
create_external_context(struct vr_executor *executor)
{
        vr_executor_get_instance_proc_cb get_instance_proc_cb =
                executor->external.get_instance_proc_cb;

        return vr_context_new_with_device(executor->config,
                                          get_instance_proc_cb,
                                          executor->external.user_data,
                                          executor->external.physical_device,
                                          executor->external.queue_family,
                                          executor->external.device,
                                          &executor->context);
}

struct vr_executor *
vr_executor_new(struct vr_config *config)
{
        struct vr_executor *executor = vr_calloc(sizeof *executor);
        executor->config = config;
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

enum vr_result
vr_executor_execute_script(struct vr_executor *executor,
                           const struct vr_script *script)
{
        enum vr_result res = VR_RESULT_PASS;
        struct vr_pipeline *pipeline = NULL;

        /* Recreate the context if the required features or extensions
         * have changed */
        if (executor->context && !context_is_compatible(executor, script))
                free_context(executor);

        const struct vr_window_format *script_window_format =
                vr_script_get_window_format(script);
        const struct vr_requirements *script_requirements =
                vr_script_get_requirements(script);

        /* Recreate the window if the framebuffer format is different */
        if (executor->window &&
            !vr_window_format_equal(&executor->window->format,
                                    script_window_format))
                free_window(executor);

        if (executor->context == NULL) {
                if (executor->use_external) {
                        res = create_external_context(executor);
                        if (res != VR_RESULT_PASS)
                                goto out;
                } else {
                        res = vr_context_new(executor->config,
                                             script_requirements,
                                             &executor->context);

                        if (res != VR_RESULT_PASS)
                                goto out;

                        executor->requirements =
                                vr_requirements_copy(script_requirements);
                }
        }

        if (executor->use_external) {
                struct vr_context *context = executor->context;

                const struct vr_vk_library *vklib =
                        vr_context_get_vklib(context);
                const struct vr_vk_instance *vkinst =
                        vr_context_get_vkinst(context);
                VkInstance vk_instance = vr_context_get_vk_instance(context);
                VkPhysicalDevice physical_device =
                        vr_context_get_physical_device(context);

                if (!vr_requirements_check(script_requirements,
                                           vklib,
                                           vkinst,
                                           vk_instance,
                                           physical_device)) {
                        char *filename = vr_script_get_filename(script);
                        vr_error_message(executor->config,
                                         "%s: A required feature or extension "
                                         "is missing",
                                         filename);
                        vr_free(filename);
                        res = VR_RESULT_SKIP;
                        goto out;
                }
        }

        if (executor->window == NULL) {
                res = vr_window_new(executor->config,
                                    executor->context,
                                    script_window_format,
                                    &executor->window);
                if (res != VR_RESULT_PASS)
                        goto out;
        }

        pipeline = vr_pipeline_create(executor->config,
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

        return res;
}

enum vr_result
vr_executor_execute(struct vr_executor *executor,
                    const struct vr_source *source)
{
        enum vr_result res = VR_RESULT_PASS;
        struct vr_script *script = NULL;
        script = vr_script_load(executor->config, source);

        if (script == NULL) {
                res = VR_RESULT_FAIL;
                goto out;
        }

        res = vr_executor_execute_script(executor, script);

out:
        if (script)
                vr_script_free(script);
        return res;
}

void
vr_executor_free(struct vr_executor *executor)
{
        free_context(executor);
        vr_free(executor);
}
