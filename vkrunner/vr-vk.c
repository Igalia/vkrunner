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
(VKAPI_PTR * func_getter)(void *object,
                          const char *func_name);

static void
init_functions(struct vr_vk *vkfn,
               func_getter getter,
               void *object,
               const struct function *functions,
               int n_functions)
{
        const struct function *function;
        int i;

        for (i = 0; i < n_functions; i++) {
                function = functions + i;
                *(void **) ((char *) vkfn + function->offset) =
                        getter(object, function->name);
        }
}

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
        vkfn->lib_vulkan = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_GLOBAL);

        if (vkfn->lib_vulkan == NULL) {
                vr_error_message(config, "Error openining libvulkan.so.1: %s",
                                 dlerror());
                return false;
        }

        vkfn->vkGetInstanceProcAddr =
                dlsym(vkfn->lib_vulkan, "vkGetInstanceProcAddr");

#endif /* WIN32 */

        init_functions(vkfn,
                       (func_getter) vkfn->vkGetInstanceProcAddr,
                       NULL, /* object */
                       core_functions,
                       VR_N_ELEMENTS(core_functions));

        return true;
}

void
vr_vk_init_instance(struct vr_vk *vkfn,
                    VkInstance instance)
{
        init_functions(vkfn,
                       (func_getter) vkfn->vkGetInstanceProcAddr,
                       instance,
                       instance_functions,
                       VR_N_ELEMENTS(instance_functions));
}

void
vr_vk_init_device(struct vr_vk *vkfn,
                  VkDevice device)
{
        init_functions(vkfn,
                       (func_getter) vkfn->vkGetDeviceProcAddr,
                       device,
                       device_functions,
                       VR_N_ELEMENTS(device_functions));
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
