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

#include "config.h"

#ifdef WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "vr-vk.h"
#include "vr-util.h"
#include "vr-error-message.h"

struct vr_vk vr_vk;

static void *lib_vulkan;

struct function {
        const char *name;
        size_t offset;
};

#define VR_VK_FUNC(func_name) \
        { .name = #func_name, .offset = offsetof(struct vr_vk, func_name) },

static const struct function
core_functions[] = {
#include "vr-vk-core-funcs.h"
};

static const struct function
instance_functions[] = {
#include "vr-vk-instance-funcs.h"
};

static const struct function
device_functions[] = {
#include "vr-vk-device-funcs.h"
};

#undef VR_VK_FUNC

typedef void *
(* func_getter)(void *object,
                const char *func_name);

static void
init_functions(func_getter getter,
               void *object,
               const struct function *functions,
               int n_functions)
{
        const struct function *function;
        int i;

        for (i = 0; i < n_functions; i++) {
                function = functions + i;
                *(void **) ((char *) &vr_vk + function->offset) =
                        getter(object, function->name);
        }
}

bool
vr_vk_load_libvulkan(void)
{
#ifdef WIN32

        lib_vulkan = LoadLibrary("vulkan-1.dll");

        if (lib_vulkan == NULL) {
                vr_error_message("Error openining vulkan-1.dll");
                return false;
        }

        vr_vk.vkGetInstanceProcAddr = (void *)
                GetProcAddress(lib_vulkan, "vkGetInstanceProcAddr");

#else
        lib_vulkan = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_GLOBAL);

        if (lib_vulkan == NULL) {
                vr_error_message("Error openining libvulkan.so.1: %s",
                                 dlerror());
                return false;
        }

        vr_vk.vkGetInstanceProcAddr =
                dlsym(lib_vulkan, "vkGetInstanceProcAddr");

#endif /* WIN32 */

        init_functions((func_getter) vr_vk.vkGetInstanceProcAddr,
                       NULL, /* object */
                       core_functions,
                       VR_N_ELEMENTS(core_functions));

        return true;
}

void
vr_vk_init_instance(VkInstance instance)
{
        init_functions((func_getter) vr_vk.vkGetInstanceProcAddr,
                       instance,
                       instance_functions,
                       VR_N_ELEMENTS(instance_functions));
}

void
vr_vk_init_device(VkDevice device)
{
        init_functions((func_getter) vr_vk.vkGetDeviceProcAddr,
                       device,
                       device_functions,
                       VR_N_ELEMENTS(device_functions));
}

void
vr_vk_unload_libvulkan(void)
{
#ifdef WIN32
        FreeLibrary(lib_vulkan);
#else
        dlclose(lib_vulkan);
#endif
}
