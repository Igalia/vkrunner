/*
 * vkrunner
 *
 * Copyright (C) 2016 Neil Roberts
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

#ifndef VR_VK_H
#define VR_VK_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include "vr-config.h"

struct vr_vk {
        void *lib_vulkan;

#define VR_VK_FUNC(name) PFN_ ## name name;
        VR_VK_FUNC(vkGetInstanceProcAddr);
        VR_VK_FUNC(vkCreateInstance);
        VR_VK_FUNC(vkEnumerateInstanceLayerProperties);
        VR_VK_FUNC(vkEnumerateInstanceExtensionProperties);
#include "vr-vk-instance-funcs.h"
#include "vr-vk-device-funcs.h"
#undef VR_VK_FUNC
};

typedef void *
(* vr_vk_get_instance_proc_cb)(const char *name,
                               void *user_data);

bool
vr_vk_load_libvulkan(const struct vr_config *config,
                     struct vr_vk *vkfn);

void
vr_vk_init_instance(struct vr_vk *vkfn,
                    vr_vk_get_instance_proc_cb get_instance_proc_cb,
                    void *user_data);

void
vr_vk_init_device(struct vr_vk *vkfn,
                  VkDevice device);

void
vr_vk_unload_libvulkan(struct vr_vk *vkfn);

#endif /* VR_VK_H */
