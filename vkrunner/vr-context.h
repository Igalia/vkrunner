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
#include "vr-requirements.h"

struct vr_context;

const VkPhysicalDeviceMemoryProperties *
vr_context_get_memory_properties(const struct vr_context *context);

const struct vr_vk_device *
vr_context_get_vkdev(const struct vr_context *context);

VkFence
vr_context_get_fence(const struct vr_context *context);

VkQueue
vr_context_get_queue(const struct vr_context *context);

VkCommandBuffer
vr_context_get_command_buffer(const struct vr_context *context);

VkPhysicalDevice
vr_context_get_physical_device(const struct vr_context *context);

VkDevice
vr_context_get_vk_device(const struct vr_context *context);

bool
vr_context_get_always_flush_memory(const struct vr_context *context);

#endif /* VR_CONTEXT_H */
