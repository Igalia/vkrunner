/*
 * vkrunner
 *
 * Copyright (C) 2017 Neil Roberts
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

#ifndef VR_CONTEXT_H
#define VR_CONTEXT_H

#include <stdbool.h>
#include "vr-vk.h"
#include "vr-result.h"
#include "vr-config.h"

struct vr_context {
        const struct vr_config *config;

        bool device_is_external;

        VkDevice device;
        VkPhysicalDevice physical_device;
        VkPhysicalDeviceMemoryProperties memory_properties;
        VkPhysicalDeviceProperties device_properties;
        VkPhysicalDeviceFeatures features;
        VkDescriptorPool descriptor_pool;
        VkCommandPool command_pool;
        VkCommandBuffer command_buffer;
        VkQueue queue;
        int queue_family;
        VkInstance vk_instance;
        VkDebugReportCallbackEXT vk_debug_callback;
        VkFence vk_fence;

        struct vr_vk vkfn;
};

enum vr_result
vr_context_new(const struct vr_config *config,
               const char *const *layers,
               const char *const *instance_extensions,
               const VkPhysicalDeviceFeatures *requires,
               const char *const *extensions,
               struct vr_context **context_out);

enum vr_result
vr_context_new_with_device(const struct vr_config *config,
                           vr_vk_get_instance_proc_cb get_instance_proc_cb,
                           void *user_data,
                           VkPhysicalDevice physical_device,
                           int queue_family,
                           VkDevice device,
                           struct vr_context **context_out);

bool
vr_context_check_extensions(struct vr_context *context,
                            const char *const *extensions);

bool
vr_context_check_features(struct vr_context *context,
                          const VkPhysicalDeviceFeatures *requires);

void
vr_context_free(struct vr_context *context);

#endif /* VR_CONTEXT_H */
