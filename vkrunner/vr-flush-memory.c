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

#include "config.h"

#include "vr-flush-memory.h"

VkResult
vr_flush_memory(struct vr_window *window,
                int memory_type_index,
                VkDeviceMemory memory,
                VkDeviceSize offset,
                VkDeviceSize size)
{
        struct vr_vk *vkfn = &window->vkfn;
        const VkMemoryType *memory_type =
                &window->memory_properties.memoryTypes[memory_type_index];

        /* We don’t need to do anything if the memory is already
         * coherent */
        if ((memory_type->propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return VK_SUCCESS;

        VkMappedMemoryRange mapped_memory_range = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = memory,
                .offset = offset,
                .size = size
        };
        return vkfn->vkFlushMappedMemoryRanges(window->device,
                                               1, /* memoryRangeCount */
                                               &mapped_memory_range);
}
