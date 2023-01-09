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

#include <stddef.h>
#include "vr-vk.h"
#include "vr-util.h"
#include "vr-error-message.h"

struct function {
        const char *name;
        size_t offset;
};

#define VR_VK_FUNC(func_name)                                           \
        {                                                               \
                .name = #func_name,                                     \
                .offset = offsetof(struct vr_vk_instance, func_name)    \
        },

static const struct function
instance_functions[] = {
#include "vr-vk-instance-funcs.h"
};

#undef VR_VK_FUNC

#define VR_VK_FUNC(func_name)                                           \
        {                                                               \
                .name = #func_name,                                     \
                .offset = offsetof(struct vr_vk_device, func_name)      \
        },

static const struct function
device_functions[] = {
#include "vr-vk-device-funcs.h"
};

#undef VR_VK_FUNC

struct vr_vk_library *
vr_vk_library_new(const struct vr_config *config)
{
        struct vr_vk_library *lib = vr_calloc(sizeof *lib);

#ifdef WIN32

        lib->lib_vulkan = LoadLibrary("vulkan-1.dll");

        if (lib->lib_vulkan == NULL) {
                vr_error_message(config, "Error openining vulkan-1.dll");
                goto error;
        }

        lib->vkGetInstanceProcAddr = (void *)
                GetProcAddress(lib->lib_vulkan, "vkGetInstanceProcAddr");
#else

#ifdef __ANDROID__
#define VULKAN_LIB "libvulkan.so"
#else
#define VULKAN_LIB "libvulkan.so.1"
#endif

        lib->lib_vulkan = dlopen(VULKAN_LIB, RTLD_LAZY | RTLD_GLOBAL);

        if (lib->lib_vulkan == NULL) {
                vr_error_message(config, "Error openining " VULKAN_LIB ": %s",
                                 dlerror());
                goto error;
        }

        lib->vkGetInstanceProcAddr =
                dlsym(lib->lib_vulkan, "vkGetInstanceProcAddr");

#endif /* WIN32 */

        lib->vkCreateInstance =
                (void *) lib->vkGetInstanceProcAddr(VK_NULL_HANDLE,
                                                    "vkCreateInstance");
        const char *func_name = "vkEnumerateInstanceExtensionProperties";
        lib->vkEnumerateInstanceExtensionProperties =
                (void *) lib->vkGetInstanceProcAddr(VK_NULL_HANDLE, func_name);

        return lib;

error:
        vr_vk_library_free(lib);
        return NULL;
}

void
vr_vk_library_free(struct vr_vk_library *lib)
{
        if (lib->lib_vulkan) {
#ifdef WIN32
                FreeLibrary(lib->lib_vulkan);
#else
                dlclose(lib->lib_vulkan);
#endif
        }

        vr_free(lib);
}

struct vr_vk_instance *
vr_vk_instance_new(vr_vk_get_instance_proc_cb get_instance_proc_cb,
                   void *user_data)
{
        struct vr_vk_instance *inst = vr_calloc(sizeof *inst);

        const struct function *function;

        for (int i = 0; i < VR_N_ELEMENTS(instance_functions); i++) {
                function = instance_functions + i;
                *(void **) ((char *) inst + function->offset) =
                        get_instance_proc_cb(function->name, user_data);
        }

        return inst;
}

void
vr_vk_instance_free(struct vr_vk_instance *inst)
{
        vr_free(inst);
}

struct vr_vk_device *
vr_vk_device_new(struct vr_vk_instance *inst,
                 VkDevice device)
{
        struct vr_vk_device *dev = vr_calloc(sizeof *dev);

        const struct function *function;

        for (int i = 0; i < VR_N_ELEMENTS(device_functions); i++) {
                function = device_functions + i;
                *(void **) ((char *) dev + function->offset) =
                        inst->vkGetDeviceProcAddr(device, function->name);
        }

        return dev;
}

void
vr_vk_device_free(struct vr_vk_device *device)
{
        vr_free(device);
}
