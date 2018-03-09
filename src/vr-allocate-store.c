/*
 * vkrunner
 *
 * Copyright (C) 2016, 2017 Neil Roberts
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

#include <stdlib.h>

#include "vr-allocate-store.h"
#include "vr-util.h"

static int
find_memory_type(struct vr_window *window,
                 uint32_t usable_memory_types,
                 uint32_t memory_type_flags)
{
        int i;

        while (usable_memory_types) {
                i = vr_util_ffs(usable_memory_types) - 1;

                if ((window->memory_properties.memoryTypes[i].propertyFlags &
                     memory_type_flags) == memory_type_flags)
                        return i;

                usable_memory_types &= ~(1 << i);
        }

        return -1;
}

VkResult
vr_allocate_store_buffer(struct vr_window *window,
                         uint32_t memory_type_flags,
                         int n_buffers,
                         const VkBuffer *buffers,
                         VkDeviceMemory *memory_out,
                         int *memory_type_index_out,
                         int *offsets)
{
        VkDeviceMemory memory;
        VkMemoryRequirements reqs;
        VkResult res;
        int offset = 0;
        int memory_type_index;
        uint32_t usable_memory_types = UINT32_MAX;
        VkDeviceSize granularity;
        int i;

        if (offsets == NULL)
                offsets = alloca(sizeof *offsets * n_buffers);

        granularity = window->device_properties.limits.bufferImageGranularity;

        for (i = 0; i < n_buffers; i++) {
                vr_vk.vkGetBufferMemoryRequirements(window->device,
                                                    buffers[i],
                                                    &reqs);
                offset = vr_align(offset, granularity);
                offset = vr_align(offset, reqs.alignment);
                offsets[i] = offset;
                offset += reqs.size;

                usable_memory_types &= reqs.memoryTypeBits;
        }

        memory_type_index = find_memory_type(window,
                                             usable_memory_types,
                                             memory_type_flags);
        if (memory_type_index == -1)
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;

        VkMemoryAllocateInfo allocate_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = offset,
                .memoryTypeIndex = memory_type_index
        };
        res = vr_vk.vkAllocateMemory(window->device,
                                     &allocate_info,
                                     NULL, /* allocator */
                                     &memory);
        if (res != VK_SUCCESS)
                return res;

        for (i = 0; i < n_buffers; i++) {
                vr_vk.vkBindBufferMemory(window->device,
                                         buffers[i],
                                         memory,
                                         offsets[i]);
        }

        *memory_out = memory;
        if (memory_type_index_out)
                *memory_type_index_out = memory_type_index;

        return VK_SUCCESS;
}

VkResult
vr_allocate_store_image(struct vr_window *window,
                        uint32_t memory_type_flags,
                        int n_images,
                        const VkImage *images,
                        VkDeviceMemory *memory_out,
                        int *memory_type_index_out)
{
        VkDeviceMemory memory;
        VkMemoryRequirements reqs;
        VkResult res;
        int offset = 0;
        int *offsets = alloca(sizeof *offsets * n_images);
        int memory_type_index;
        uint32_t usable_memory_types = UINT32_MAX;
        VkDeviceSize granularity;
        int i;

        granularity = window->device_properties.limits.bufferImageGranularity;

        for (i = 0; i < n_images; i++) {
                vr_vk.vkGetImageMemoryRequirements(window->device,
                                                   images[i],
                                                   &reqs);
                offset = vr_align(offset, granularity);
                offset = vr_align(offset, reqs.alignment);
                offsets[i] = offset;
                offset += reqs.size;

                usable_memory_types &= reqs.memoryTypeBits;
        }

        memory_type_index = find_memory_type(window,
                                             usable_memory_types,
                                             memory_type_flags);
        if (memory_type_index == -1)
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;

        VkMemoryAllocateInfo allocate_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize = offset,
                .memoryTypeIndex = memory_type_index
        };
        res = vr_vk.vkAllocateMemory(window->device,
                                     &allocate_info,
                                     NULL, /* allocator */
                                     &memory);
        if (res != VK_SUCCESS)
                return res;

        for (i = 0; i < n_images; i++) {
                vr_vk.vkBindImageMemory(window->device,
                                        images[i],
                                        memory,
                                        offsets[i]);
        }

        *memory_out = memory;
        if (memory_type_index_out)
                *memory_type_index_out = memory_type_index;

        return VK_SUCCESS;
}
