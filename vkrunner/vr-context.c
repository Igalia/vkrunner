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

#include "vr-context.h"
#include "vr-util.h"
#include "vr-error-message.h"
#include "vr-allocate-store.h"
#include "vr-feature-offsets.h"

static int
find_queue_family(struct vr_context *context,
                  VkPhysicalDevice physical_device)
{
        struct vr_vk *vkfn = &context->vkfn;
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
        struct vr_vk *vkfn = &context->vkfn;

        if (context->vk_fence) {
                vkfn->vkDestroyFence(context->device,
                                     context->vk_fence,
                                     NULL /* allocator */);
                context->vk_fence = VK_NULL_HANDLE;
        }
        if (context->descriptor_pool) {
                vkfn->vkDestroyDescriptorPool(context->device,
                                              context->descriptor_pool,
                                              NULL /* allocator */);
                context->descriptor_pool = VK_NULL_HANDLE;
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
                if (context->vk_instance && context->vk_debug_callback) {
                        vkfn->vkDestroyDebugReportCallbackEXT(
                                        context->vk_instance,
                                        context->vk_debug_callback,
                                        NULL);
                }
                if (context->vk_instance) {
                        vkfn->vkDestroyInstance(context->vk_instance,
                                                NULL /* allocator */);
                        context->vk_instance = VK_NULL_HANDLE;
                }
        }
}

#define FIND(what, property)                                      \
        static bool                                               \
        find_##what(uint32_t property_count,                      \
                    const property *props,                        \
                    const char *name)                             \
        {                                                         \
                for (uint32_t i = 0; i < property_count; i++) {   \
                        if (!strcmp(name, props[i].what##Name))   \
                                return true;                      \
                }                                                 \
                                                                  \
                return false;                                     \
        }
FIND(extension, VkExtensionProperties);  // find_extension
FIND(layer, VkLayerProperties);          // find_layer

static VkResult
get_extensions_with_alloc(struct vr_vk *vkfn,
                          const char *layer,
                          VkExtensionProperties **pprops,
                          uint32_t *pproperty_count)
{
        VkResult res = vkfn->vkEnumerateInstanceExtensionProperties(
                        layer,
                        pproperty_count,
                        NULL);
        if (pprops == NULL || res != VK_SUCCESS)
                return res;

        *pprops = vr_alloc(*pproperty_count * sizeof(VkExtensionProperties));

        res = vkfn->vkEnumerateInstanceExtensionProperties(layer,
                                                           pproperty_count,
                                                           *pprops);
        return res;
}

static bool
check_layers_and_extensions(struct vr_context *context,
                            const char *const *layers,
                            const char *const *extensions,
                            uint32_t *p_n_layers,
                            uint32_t *p_n_instance_extensions)
{
        struct vr_vk *vkfn = &context->vkfn;
        VkResult res;
        uint32_t property_count;
        bool ret = true;

        *p_n_layers = 0;
        *p_n_instance_extensions = 0;

        if (!layers || !*layers || !extensions || !*extensions)
                return false;

        res = vkfn->vkEnumerateInstanceLayerProperties(&property_count,
                                                       NULL);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Error enumerate instance layers");
                return false;
        }

        VkLayerProperties *props = vr_alloc(property_count * sizeof *props);

        res = vkfn->vkEnumerateInstanceLayerProperties(&property_count,
                                                       props);
        if (res == VK_SUCCESS) {
                // Check layers
                *p_n_layers = 0;
                for (const char * const *layer = layers; *layer; layer++) {
                        if (!find_layer(property_count, props, *layer)) {
                                vr_error_message(context->config,
                                                 "Error layer '%s' not found",
                                                 *layer);
                                ret = false;
                        }
                        ++*p_n_layers;
                }
        } else {
                vr_error_message(context->config,
                                 "Error enumerate instance layers");
                ret = false;
        }

        if (ret) {
                // Check extensions
                VkExtensionProperties **extension_array =
                        vr_alloc(*p_n_layers * sizeof *extension_array);
                uint32_t *n_extensions =
                        vr_alloc(*p_n_layers * sizeof *n_extensions);
                for (uint32_t i = 0; i < *p_n_layers; i++) {
                        res = get_extensions_with_alloc(vkfn,
                                                        layers[i],
                                                        &extension_array[i],
                                                        &n_extensions[i]);
                        if (res != VK_SUCCESS) {
                                vr_error_message(context->config,
                                                 "Failed to get instance "
                                                 "extensions");
                                ret = false;
                                break;
                        }
                }
                for (const char * const *ext = extensions; *ext; ext++) {
                        bool found = false;
                        for (uint32_t i = 0; i < *p_n_layers; i++) {
                                if (!extension_array[i])
                                        continue;
                                if (find_extension(n_extensions[i],
                                                   extension_array[i],
                                                   *ext))
                                        found = true;
                        }
                        if (!found) {
                                vr_error_message(context->config,
                                                 "Error extension '%s' "
                                                 "not found",
                                                 *ext);
                                ret = false;
                        }
                        ++*p_n_instance_extensions;
                }
                for (uint32_t i = 0; i < *p_n_layers; i++)
                        vr_free(extension_array[i]);
                vr_free(extension_array);
                vr_free(n_extensions);
        } else {
                // Show available layers and extensions for debugging
                vr_error_message(context->config,
                                 "Available instance layers and "
                                 "their instance extensions:\n");
                for (uint32_t i = 0; i < property_count; i++) {
                        vr_error_message(context->config,
                                         "Layer '%s'",
                                         props[i].layerName);
                        uint32_t ext_count;
                        VkExtensionProperties *ext = NULL;
                        res = get_extensions_with_alloc(vkfn,
                                                        props[i].layerName,
                                                        &ext,
                                                        &ext_count);
                        if (res == VK_SUCCESS) {
                                for (uint32_t j = 0; j < ext_count; j++) {
                                        vr_error_message(context->config,
                                                         "\tExtension '%s'",
                                                         ext[j].extensionName);
                                }
                        }
                        if (ext)
                                vr_free(ext);
                }
        }
        vr_free(props);

        return ret;
}

static bool
get_feature(const VkPhysicalDeviceFeatures *features,
            int feature_num)
{
        const struct vr_feature_offset *fo =
                vr_feature_offsets + feature_num;
        return *(const VkBool32 *) ((const uint8_t *) features + fo->offset);
}

static bool
check_features(const VkPhysicalDeviceFeatures *features,
               const VkPhysicalDeviceFeatures *requires)
{
        for (int i = 0; vr_feature_offsets[i].name; i++) {
                if (get_feature(requires, i) &&
                    !get_feature(features, i)) {
                        return false;
                }
        }

        return true;
}

static bool
check_extensions(struct vr_context *context,
                 VkPhysicalDevice device,
                 const char *const *extensions)
{
        struct vr_vk *vkfn = &context->vkfn;
        VkResult res;
        uint32_t property_count;

        res = vkfn->vkEnumerateDeviceExtensionProperties(device,
                                                         NULL, /* layerName */
                                                         &property_count,
                                                         NULL /* properties */);
        if (res != VK_SUCCESS)
                return false;

        VkExtensionProperties *props = vr_alloc(property_count * sizeof *props);
        bool ret = true;

        res = vkfn->vkEnumerateDeviceExtensionProperties(device,
                                                         NULL, /* layerName */
                                                         &property_count,
                                                         props);
        if (res == VK_SUCCESS) {
                for (const char * const *ext = extensions; *ext; ext++) {
                        if (!find_extension(property_count, props, *ext)) {
                                ret = false;
                                break;
                        }
                }
        } else {
                ret = false;
        }

        vr_free(props);

        return ret;
}

static enum vr_result
find_physical_device(struct vr_context *context,
                     const VkPhysicalDeviceFeatures *requires,
                     const char *const *extensions)

{
        struct vr_vk *vkfn = &context->vkfn;
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

        for (i = 0; i < count; i++) {
                VkPhysicalDeviceFeatures features;
                vkfn->vkGetPhysicalDeviceFeatures(devices[i], &features);
                if (!check_features(&features, requires))
                        continue;

                if (!check_extensions(context, devices[i], extensions))
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
        struct vr_vk *vkfn = &context->vkfn;

        return vkfn->vkGetInstanceProcAddr(context->vk_instance, name);
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugReportFlagsEXT flags,
               VkDebugReportObjectTypeEXT obj_type,
               uint64_t obj,
               size_t location,
               int32_t code,
               const char* layer_prefix,
               const char* message,
               void* user_data)
{
        const char *level = NULL;
        const struct vr_config *config = user_data;

        if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
                level = "ERROR";
        else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
                level = "WARNING";
        else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
                level = "INFO";
        else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
                level = "DEBUG";

        vr_error_message(config,
                         "[%s] validation layer '%s': %s",
                         level,
                         layer_prefix,
                         message);
        return VK_FALSE;
}

static bool
set_debug_callback(struct vr_context *context)
{
        struct vr_vk *vkfn = &context->vkfn;
        VkResult res;

        VkDebugReportCallbackCreateInfoEXT info = {
                .sType =
                VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
                .flags = VK_DEBUG_REPORT_ERROR_BIT_EXT |
                         VK_DEBUG_REPORT_WARNING_BIT_EXT |
                         VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
                         VK_DEBUG_REPORT_DEBUG_BIT_EXT,
                .pfnCallback = debug_callback,
                .pUserData = (void *) context->config
        };

        res = vkfn->vkCreateDebugReportCallbackEXT(context->vk_instance,
                                                   &info,
                                                   NULL,
                                                   &context->vk_debug_callback);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Failed to set debug callback");
                return false;
        }
        return true;
}

static enum vr_result
init_vk_device(struct vr_context *context,
               const char *const *layers,
               const char *const *instance_extensions,
               const VkPhysicalDeviceFeatures *requires,
               const char *const *extensions)
{
        struct vr_vk *vkfn = &context->vkfn;
        VkResult res;

        uint32_t n_layers;
        uint32_t n_instance_extensions;

        bool layer_on = check_layers_and_extensions(context,
                                                    layers,
                                                    instance_extensions,
                                                    &n_layers,
                                                    &n_instance_extensions);

        struct VkInstanceCreateInfo instance_create_info = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &(VkApplicationInfo) {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .pApplicationName = "vkrunner",
                        .apiVersion = VK_MAKE_VERSION(1, 0, 2)
                },
                .enabledLayerCount = n_layers,
                .ppEnabledLayerNames = layers,
                .enabledExtensionCount = n_instance_extensions,
                .ppEnabledExtensionNames = instance_extensions
        };
        res = vkfn->vkCreateInstance(&instance_create_info,
                                     NULL, /* allocator */
                                     &context->vk_instance);

        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Failed to create VkInstance");
                return VR_RESULT_FAIL;
        }

        vr_vk_init_instance(vkfn, get_instance_proc, context);
        if (layer_on)
                set_debug_callback(context);

        enum vr_result vres =
                find_physical_device(context, requires, extensions);
        if (vres != VR_RESULT_PASS)
                return vres;

        int n_extensions = 0;
        for (const char * const *ext = extensions; *ext; ext++)
                n_extensions++;

        VkDeviceCreateInfo device_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .queueFamilyIndex = context->queue_family,
                        .queueCount = 1,
                        .pQueuePriorities = (float[]) { 1.0f }
                },
                .enabledExtensionCount = n_extensions,
                .ppEnabledExtensionNames = n_extensions ? extensions : NULL,
                .pEnabledFeatures = requires
        };
        res = vkfn->vkCreateDevice(context->physical_device,
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
        struct vr_vk *vkfn = &context->vkfn;
        VkPhysicalDeviceMemoryProperties *memory_properties =
                &context->memory_properties;
        enum vr_result vres = VR_RESULT_PASS;
        VkResult res;

        vkfn->vkGetPhysicalDeviceProperties(context->physical_device,
                                            &context->device_properties);
        vkfn->vkGetPhysicalDeviceMemoryProperties(context->physical_device,
                                                  memory_properties);
        vkfn->vkGetPhysicalDeviceFeatures(context->physical_device,
                                          &context->features);

        vr_vk_init_device(vkfn, context->device);

        vkfn->vkGetDeviceQueue(context->device,
                               context->queue_family,
                               0, /* queueIndex */
                               &context->queue);

        VkCommandPoolCreateInfo command_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = context->queue_family
        };
        res = vkfn->vkCreateCommandPool(context->device,
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
        res = vkfn->vkAllocateCommandBuffers(context->device,
                                             &command_buffer_allocate_info,
                                             &context->command_buffer);

        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Error creating command buffer");
                vres = VR_RESULT_FAIL;
                goto error;
        }

        VkDescriptorPoolSize pool_sizes[] = {
                {
                        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = 4
                },
                {
                        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .descriptorCount = 4
                },
                {
                        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .descriptorCount = 4
                },
        };
        VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets = 4,
                .poolSizeCount = VR_N_ELEMENTS(pool_sizes),
                .pPoolSizes = pool_sizes
        };
        res = vkfn->vkCreateDescriptorPool(context->device,
                                           &descriptor_pool_create_info,
                                           NULL, /* allocator */
                                           &context->descriptor_pool);
        if (res != VK_SUCCESS) {
                vr_error_message(context->config,
                                 "Error creating VkDescriptorPool");
                vres = VR_RESULT_FAIL;
                goto error;
        }

        VkFenceCreateInfo fence_create_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        res = vkfn->vkCreateFence(context->device,
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

bool
vr_context_check_extensions(struct vr_context *context,
                            const char *const *extensions)
{
        return check_extensions(context, context->physical_device, extensions);
}

bool
vr_context_check_features(struct vr_context *context,
                          const VkPhysicalDeviceFeatures *requires)
{
        return check_features(&context->features, requires);
}

enum vr_result
vr_context_new(const struct vr_config *config,
               const char *const *layers,
               const char *const *instance_extensions,
               const VkPhysicalDeviceFeatures *requires,
               const char *const *extensions,
               struct vr_context **context_out)
{
        struct vr_context *context = vr_calloc(sizeof *context);
        struct vr_vk *vkfn = &context->vkfn;
        enum vr_result vres;

        if (!vr_vk_load_libvulkan(config, vkfn)) {
                vres = VR_RESULT_FAIL;
                goto error;
        }

        context->config = config;

        vres = init_vk_device(context,
                              layers,
                              instance_extensions,
                              requires,
                              extensions);
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
        struct vr_vk *vkfn = &context->vkfn;
        enum vr_result vres;

        context->config = config;
        context->physical_device = physical_device;
        context->queue_family = queue_family;
        context->device = device;
        context->device_is_external = true;

        vr_vk_init_instance(vkfn, get_instance_proc_cb, user_data);

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

        if (!context->device_is_external && context->vkfn.lib_vulkan)
                vr_vk_unload_libvulkan(&context->vkfn);

        vr_free(context);
}
