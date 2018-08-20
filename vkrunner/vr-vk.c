/*
 * vkrunner
 *
 * Copyright (C) 2016 Neil Roberts
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

#ifdef WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "vr-vk.h"
#include "vr-util.h"
#include "vr-error-message.h"

struct function {
        const char *name;
        size_t offset;
};

#define VR_VK_FUNC(func_name) \
        { .name = #func_name, .offset = offsetof(struct vr_vk, func_name) },

static const struct function
instance_functions[] = {
#include "vr-vk-instance-funcs.h"
};

static const struct function
device_functions[] = {
#include "vr-vk-device-funcs.h"
};

#undef VR_VK_FUNC

bool
vr_vk_load_libvulkan(const struct vr_config *config,
                     struct vr_vk *vkfn)
{
#ifdef WIN32

        vkfn->lib_vulkan = LoadLibrary("vulkan-1.dll");

        if (vkfn->lib_vulkan == NULL) {
                vr_error_message(config, "Error openining vulkan-1.dll");
                return false;
        }

        vkfn->vkGetInstanceProcAddr = (void *)
                GetProcAddress(vkfn->lib_vulkan, "vkGetInstanceProcAddr");
#else

#ifdef __ANDROID__
#define VULKAN_LIB "libvulkan.so"
#else
#define VULKAN_LIB "libvulkan.so.1"
#endif

        vkfn->lib_vulkan = dlopen(VULKAN_LIB, RTLD_LAZY | RTLD_GLOBAL);

        if (vkfn->lib_vulkan == NULL) {
                vr_error_message(config, "Error openining " VULKAN_LIB ": %s",
                                 dlerror());
                return false;
        }

        vkfn->vkGetInstanceProcAddr =
                dlsym(vkfn->lib_vulkan, "vkGetInstanceProcAddr");

#endif /* WIN32 */

        vkfn->vkCreateInstance =
                (void *) vkfn->vkGetInstanceProcAddr(VK_NULL_HANDLE,
                                                     "vkCreateInstance");
        vkfn->vkEnumerateInstanceLayerProperties =
                (void *) vkfn->vkGetInstanceProcAddr(
                    VK_NULL_HANDLE,
                    "vkEnumerateInstanceLayerProperties");
        vkfn->vkEnumerateInstanceExtensionProperties =
                (void *) vkfn->vkGetInstanceProcAddr(
                    VK_NULL_HANDLE,
                    "vkEnumerateInstanceExtensionProperties");
        return true;
}

void
vr_vk_init_instance(struct vr_vk *vkfn,
                    vr_vk_get_instance_proc_cb get_instance_proc_cb,
                    void *user_data)
{
        const struct function *function;

        for (int i = 0; i < VR_N_ELEMENTS(instance_functions); i++) {
                function = instance_functions + i;
                *(void **) ((char *) vkfn + function->offset) =
                        get_instance_proc_cb(function->name, user_data);
        }
}

void
vr_vk_init_device(struct vr_vk *vkfn,
                  VkDevice device)
{
        const struct function *function;

        for (int i = 0; i < VR_N_ELEMENTS(device_functions); i++) {
                function = device_functions + i;
                *(void **) ((char *) vkfn + function->offset) =
                        vkfn->vkGetDeviceProcAddr(device, function->name);
        }
}

void
vr_vk_unload_libvulkan(struct vr_vk *vkfn)
{
#ifdef WIN32
        FreeLibrary(vkfn->lib_vulkan);
#else
        dlclose(vkfn->lib_vulkan);
#endif
}
