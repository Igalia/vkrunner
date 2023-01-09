/*
 * vkrunner
 *
 * Copyright (C) 2013, 2014, 2015, 2017 Neil Roberts
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

#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "vr-config-private.h"
#include "vr-context.h"
#include "vr-util.h"
#include "vr-error-message.h"
#include "vr-allocate-store.h"

static int
find_queue_family(struct vr_context *context,
                  VkPhysicalDevice physical_device)
{
        struct vr_vk_instance *vkfn = context->vkinst;
        VkQueueFamilyProperties *queues;
        uint32_t count = 0;
        uint32_t i;

        vkfn->vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       NULL /* queues */);

        queues = vr_alloc(sizeof *queues * count);

        vkfn->vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       queues);

        for (i = 0; i < count; i++) {
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                    queues[i].queueCount >= 1)
                        break;
        }

        vr_free(queues);

        if (i >= count)
                return -1;
        else
                return i;
}

static void
deinit_vk(struct vr_context *context)
{
        struct vr_vk_device *vkfn = context->vkdev;

        if (context->vk_fence) {
                vkfn->vkDestroyFence(context->device,
                                     context->vk_fence,
                                     NULL /* allocator */);
                context->vk_fence = VK_NULL_HANDLE;
        }
        if (context->command_buffer) {
                vkfn->vkFreeCommandBuffers(context->device,
                                           context->command_pool,
                                           1, /* commandBufferCount */
                                           &context->command_buffer);
                context->command_buffer = VK_NULL_HANDLE;
        }
        if (context->command_pool) {
                vkfn->vkDestroyCommandPool(context->device,
                                           context->command_pool,
                                           NULL /* allocator */);
                context->command_pool = VK_NULL_HANDLE;
        }
        if (!context->device_is_external) {

                if (context->device) {
                        vkfn->vkDestroyDevice(context->device,
                                              NULL /* allocator */);
                        context->device = VK_NULL_HANDLE;
                }
                if (context->vk_instance) {
                        struct vr_vk_instance *vkinst = context->vkinst;

                        vkinst->vkDestroyInstance(context->vk_instance,
                                                  NULL /* allocator */);
                        context->vk_instance = VK_NULL_HANDLE;
                }
        }
        if (context->vkdev) {
                vr_vk_device_free(context->vkdev);
                context->vkdev = NULL;
        }
}

static enum vr_result
find_physical_device(struct vr_context *context,
                     const struct vr_requirements *reqs)

{
        struct vr_vk_instance *vkfn = context->vkinst;
        VkResult res;
        uint32_t count;
        VkPhysicalDevice *devices;
        int i, queue_family;

        res = vkfn->vkEnumeratePhysicalDevices(context->vk_instance,
                                               &count,
                                               NULL);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Error enumerating VkPhysicalDevices");
                return VR_RESULT_FAIL;
        }

        devices = alloca(count * sizeof *devices);

        res = vkfn->vkEnumeratePhysicalDevices(context->vk_instance,
                                               &count,
                                               devices);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Error enumerating VkPhysicalDevices");
                return VR_RESULT_FAIL;
        }

        int dev_id = context->config->device_id;
        int first_dev = 0;
        if (dev_id >= 0) {
                if (dev_id >= count) {
                        vr_error_message(context->config,
                                         "Error unsupported device id.");
                        return VR_RESULT_SKIP;
                }

                first_dev = dev_id;
                count = first_dev + 1;
        }
        for (i = first_dev; i < count; i++) {
                if (!vr_requirements_check(reqs,
                                           context->vklib,
                                           context->vkinst,
                                           context->vk_instance,
                                           devices[i]))
                        continue;

                queue_family = find_queue_family(context, devices[i]);
                if (queue_family == -1)
                        continue;

                context->physical_device = devices[i];
                context->queue_family = queue_family;

                return VR_RESULT_PASS;
        }

        vr_error_message(context->config,
                         "No suitable device and queue family found");

        return VR_RESULT_SKIP;
}

static void *
get_instance_proc(const char *name,
                  void *user_data)
{
        struct vr_context *context = user_data;
        struct vr_vk_library *vklib = context->vklib;

        return vklib->vkGetInstanceProcAddr(context->vk_instance, name);
}

static bool
find_extension(uint32_t property_count,
               const VkExtensionProperties *props,
               const char *extension)
{
        for (uint32_t i = 0; i < property_count; i++) {
                if (!strcmp(extension, props[i].extensionName))
                        return true;
        }

        return false;
}

static bool
check_instance_extension(struct vr_vk_library *vkfn,
                         const char *ext)
{
        VkResult res;
        uint32_t count;

        res = vkfn->vkEnumerateInstanceExtensionProperties(NULL, /* layerName */
                                                           &count,
                                                           NULL /* props */);

        if (res != VK_SUCCESS)
                return false;

        VkExtensionProperties *props = vr_alloc(count * sizeof *props);
        bool ret = true;

        res = vkfn->vkEnumerateInstanceExtensionProperties(NULL, /* layerName */
                                                           &count,
                                                           props);
        if (res != VK_SUCCESS && !find_extension(count, props, ext))
                ret = false;

        vr_free(props);

        return ret;
}

static enum vr_result
init_vk_device(struct vr_context *context,
               const struct vr_requirements *reqs)
{
        struct vr_vk_library *vklib = context->vklib;
        VkResult res;

        struct VkInstanceCreateInfo instance_create_info = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &(VkApplicationInfo) {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .pApplicationName = "vkrunner",
                        .apiVersion = vr_requirements_get_version(reqs)
                },
        };

        const char *ext =
                VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

        if (vr_requirements_get_structures(reqs)) {
                if (!check_instance_extension(vklib, ext)) {
                        vr_error_message(context->config,
                                         "Driver is missing the %s extension",
                                         ext);
                        return VR_RESULT_SKIP;
                }

                instance_create_info.enabledExtensionCount = 1;
                instance_create_info.ppEnabledExtensionNames = &ext;
        }

        res = vklib->vkCreateInstance(&instance_create_info,
                                      NULL, /* allocator */
                                      &context->vk_instance);

        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Failed to create VkInstance");
                if (res == VK_ERROR_INCOMPATIBLE_DRIVER) {
                        /* The loader reports this if there are no
                         * drivers available at all. In that case we
                         * want to skip.
                         */
                        return VR_RESULT_SKIP;
                } else {
                        return VR_RESULT_FAIL;
                }
        }

        context->vkinst = vr_vk_instance_new(get_instance_proc, context);

        struct vr_vk_instance *vkinst = context->vkinst;

        enum vr_result vres =
                find_physical_device(context, reqs);
        if (vres != VR_RESULT_PASS)
                return vres;

        size_t n_extensions = vr_requirements_get_n_extensions(reqs);

        VkDeviceCreateInfo device_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .pNext = vr_requirements_get_structures(reqs),
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .queueFamilyIndex = context->queue_family,
                        .queueCount = 1,
                        .pQueuePriorities = (float[]) { 1.0f }
                },
                .enabledExtensionCount = n_extensions,
                .ppEnabledExtensionNames =
                (n_extensions > 0 ?
                 vr_requirements_get_extensions(reqs) :
                 NULL),
                .pEnabledFeatures = vr_requirements_get_base_features(reqs)
        };
        res = vkinst->vkCreateDevice(context->physical_device,
                                     &device_create_info,
                                     NULL, /* allocator */
                                     &context->device);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config, "Error creating VkDevice");
                return VR_RESULT_FAIL;
        }

        return VR_RESULT_PASS;
}

static enum vr_result
init_vk(struct vr_context *context)
{
        struct vr_vk_instance *vkinst = context->vkinst;
        VkPhysicalDeviceMemoryProperties *memory_properties =
                &context->memory_properties;
        enum vr_result vres = VR_RESULT_PASS;
        VkResult res;

        vkinst->vkGetPhysicalDeviceProperties(context->physical_device,
                                              &context->device_properties);
        vkinst->vkGetPhysicalDeviceMemoryProperties(context->physical_device,
                                                    memory_properties);
        vkinst->vkGetPhysicalDeviceFeatures(context->physical_device,
                                            &context->features);

        context->vkdev = vr_vk_device_new(vkinst, context->device);

        struct vr_vk_device *vkdev = context->vkdev;

        vkdev->vkGetDeviceQueue(context->device,
                                context->queue_family,
                                0, /* queueIndex */
                                &context->queue);

        VkCommandPoolCreateInfo command_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = context->queue_family
        };
        res = vkdev->vkCreateCommandPool(context->device,
                                         &command_pool_create_info,
                                         NULL, /* allocator */
                                         &context->command_pool);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Error creating VkCommandPool");
                vres = VR_RESULT_FAIL;
                goto error;
        }

        VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = context->command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
        };
        res = vkdev->vkAllocateCommandBuffers(context->device,
                                              &command_buffer_allocate_info,
                                              &context->command_buffer);

        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Error creating command buffer");
                vres = VR_RESULT_FAIL;
                goto error;
        }

        VkFenceCreateInfo fence_create_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        res = vkdev->vkCreateFence(context->device,
                                   &fence_create_info,
                                   NULL, /* allocator */
                                   &context->vk_fence);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config, "Error creating fence");
                vres = VR_RESULT_FAIL;
                goto error;
        }

        return VR_RESULT_PASS;

error:
        deinit_vk(context);
        return vres;
}

enum vr_result
vr_context_new(const struct vr_config *config,
               const struct vr_requirements *reqs,
               struct vr_context **context_out)
{
        struct vr_context *context = vr_calloc(sizeof *context);
        enum vr_result vres;

        context->vklib = vr_vk_library_new(config);

        if (context->vklib == NULL) {
                vres = VR_RESULT_FAIL;
                goto error;
        }

        context->config = config;
        context->always_flush_memory =
                vr_env_var_as_boolean("VKRUNNER_ALWAYS_FLUSH_MEMORY", false);

        vres = init_vk_device(context, reqs);

        if (vres != VR_RESULT_PASS)
                goto error;

        vres = init_vk(context);
        if (vres != VR_RESULT_PASS)
                goto error;

        *context_out = context;
        return VR_RESULT_PASS;

error:
        vr_context_free(context);
        return vres;
}

enum vr_result
vr_context_new_with_device(const struct vr_config *config,
                           vr_vk_get_instance_proc_cb get_instance_proc_cb,
                           void *user_data,
                           VkPhysicalDevice physical_device,
                           int queue_family,
                           VkDevice device,
                           struct vr_context **context_out)
{
        struct vr_context *context = vr_calloc(sizeof *context);
        enum vr_result vres;

        context->config = config;
        context->physical_device = physical_device;
        context->queue_family = queue_family;
        context->device = device;
        context->device_is_external = true;

        context->vkinst = vr_vk_instance_new(get_instance_proc_cb, user_data);

        vres = init_vk(context);
        if (vres != VR_RESULT_PASS)
                goto error;

        *context_out = context;
        return VR_RESULT_PASS;

error:
        vr_context_free(context);
        return vres;
}

void
vr_context_free(struct vr_context *context)
{
        deinit_vk(context);

        if (context->vkinst)
                vr_vk_instance_free(context->vkinst);

        if (context->vklib)
                vr_vk_library_free(context->vklib);

        vr_free(context);
}
