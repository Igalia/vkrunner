/*
 * vkrunner
 *
 * Copyright (C) 2017 Neil Roberts
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

#ifndef VR_WINDOW_H
#define VR_WINDOW_H

#include <stdbool.h>
#include "vr-vk.h"

#define VR_WINDOW_WIDTH 250
#define VR_WINDOW_HEIGHT 250

struct vr_window {
        VkDevice device;
        VkPhysicalDevice physical_device;
        VkPhysicalDeviceMemoryProperties memory_properties;
        VkPhysicalDeviceProperties device_properties;
        VkPhysicalDeviceFeatures features;
        VkDescriptorPool descriptor_pool;
        VkCommandPool command_pool;
        VkCommandBuffer command_buffer;
        VkRenderPass render_pass;
        VkQueue queue;
        int queue_family;
        VkInstance vk_instance;
        VkFence vk_fence;

        VkImage color_image;
        VkImage linear_image;
        VkDeviceMemory memory;
        VkDeviceMemory linear_memory;
        bool need_linear_memory_invalidate;
        void *linear_memory_map;
        VkDeviceSize linear_memory_stride;
        VkImageView color_image_view;
        VkFramebuffer framebuffer;

        bool libvulkan_loaded;
};

struct vr_window *
vr_window_new(const struct VkPhysicalDeviceFeatures *requires);

void
vr_window_free(struct vr_window *window);

#endif /* VR_WINDOW_H */
