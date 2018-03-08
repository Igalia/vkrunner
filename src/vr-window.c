/*
 * vkrunner
 *
 * Copyright (C) 2013, 2014, 2015, 2017 Neil Roberts
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

#include "vr-window.h"
#include "vr-util.h"
#include "vr-error-message.h"

#define COLOR_IMAGE_FORMAT VK_FORMAT_B8G8R8A8_SRGB

static int
find_queue_family(struct vr_window *window,
                  VkPhysicalDevice physical_device)
{
        VkQueueFamilyProperties *queues;
        uint32_t count = 0;
        uint32_t i;

        vr_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                                       &count,
                                                       NULL /* queues */);

        queues = vr_alloc(sizeof *queues * count);

        vr_vk.vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
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
deinit_vk(struct vr_window *window)
{
        if (window->vk_fence) {
                vr_vk.vkDestroyFence(window->device,
                                     window->vk_fence,
                                     NULL /* allocator */);
                window->vk_fence = NULL;
        }
        if (window->render_pass) {
                vr_vk.vkDestroyRenderPass(window->device,
                                          window->render_pass,
                                          NULL /* allocator */);
                window->render_pass = NULL;
        }
        if (window->descriptor_pool) {
                vr_vk.vkDestroyDescriptorPool(window->device,
                                              window->descriptor_pool,
                                              NULL /* allocator */);
                window->descriptor_pool = NULL;
        }
        if (window->command_buffer) {
                vr_vk.vkFreeCommandBuffers(window->device,
                                           window->command_pool,
                                           1, /* commandBufferCount */
                                           &window->command_buffer);
                window->command_buffer = NULL;
        }
        if (window->command_pool) {
                vr_vk.vkDestroyCommandPool(window->device,
                                           window->command_pool,
                                           NULL /* allocator */);
                window->command_pool = NULL;
        }
        if (window->vk_semaphore) {
                vr_vk.vkDestroySemaphore(window->device,
                                         window->vk_semaphore,
                                         NULL /* allocator */);
                window->vk_semaphore = NULL;
        }
        if (window->device) {
                vr_vk.vkDestroyDevice(window->device,
                                      NULL /* allocator */);
                window->device = NULL;
        }
        if (window->vk_instance) {
                vr_vk.vkDestroyInstance(window->vk_instance,
                                        NULL /* allocator */);
                window->vk_instance = NULL;
        }
}

static bool
find_physical_device(struct vr_window *window)
{
        VkResult res;
        uint32_t count;
        VkPhysicalDevice *devices;
        int i, queue_family;

        res = vr_vk.vkEnumeratePhysicalDevices(window->vk_instance,
                                               &count,
                                               NULL);
        if (res != VK_SUCCESS) {
                vr_error_message("Error enumerating VkPhysicalDevices");
                return false;
        }

        devices = alloca(count * sizeof *devices);

        res = vr_vk.vkEnumeratePhysicalDevices(window->vk_instance,
                                               &count,
                                               devices);
        if (res != VK_SUCCESS) {
                vr_error_message("Error enumerating VkPhysicalDevices");
                return false;
        }

        for (i = 0; i < count; i++) {
                queue_family = find_queue_family(window, devices[i]);
                if (queue_family == -1)
                        continue;

                window->physical_device = devices[i];
                window->queue_family = queue_family;

                return true;
        }

        vr_error_message("No suitable device and queue family found");
        return false;
}

static bool
init_vk(struct vr_window *window)
{
        VkPhysicalDeviceMemoryProperties *memory_properties =
                &window->memory_properties;
        VkResult res;

        struct VkInstanceCreateInfo instance_create_info = {
                .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                .pApplicationInfo = &(VkApplicationInfo) {
                        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                        .pApplicationName = "vkrunner",
                        .apiVersion = VK_MAKE_VERSION(1, 0, 2)
                },
        };
        res = vr_vk.vkCreateInstance(&instance_create_info,
                                     NULL, /* allocator */
                                     &window->vk_instance);

        if (res != VK_SUCCESS) {
                vr_error_message("Failed to create VkInstance");
                goto error;
        }

        vr_vk_init_instance(window->vk_instance);

        if (!find_physical_device(window))
                goto error;

        vr_vk.vkGetPhysicalDeviceProperties(window->physical_device,
                                            &window->device_properties);
        vr_vk.vkGetPhysicalDeviceMemoryProperties(window->physical_device,
                                                  memory_properties);
        vr_vk.vkGetPhysicalDeviceFeatures(window->physical_device,
                                          &window->features);

        VkDeviceCreateInfo device_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .queueFamilyIndex = window->queue_family,
                        .queueCount = 1,
                        .pQueuePriorities = (float[]) { 1.0f }
                },
                .enabledExtensionCount = 1,
                .ppEnabledExtensionNames = (const char * const []) {
                        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                },
        };
        res = vr_vk.vkCreateDevice(window->physical_device,
                                   &device_create_info,
                                   NULL, /* allocator */
                                   &window->device);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating VkDevice");
                goto error;
        }

        vr_vk_init_device(window->device);

        vr_vk.vkGetDeviceQueue(window->device,
                               window->queue_family,
                               0, /* queueIndex */
                               &window->queue);

        VkSemaphoreCreateInfo semaphore_create_info = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        res = vr_vk.vkCreateSemaphore(window->device,
                                      &semaphore_create_info,
                                      NULL, /* allocator */
                                      &window->vk_semaphore);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating semaphore");
                goto error;
        }

        VkCommandPoolCreateInfo command_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = window->queue_family
        };
        res = vr_vk.vkCreateCommandPool(window->device,
                                        &command_pool_create_info,
                                        NULL, /* allocator */
                                        &window->command_pool);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating VkCommandPool");
                goto error;
        }

        VkCommandBufferAllocateInfo command_buffer_allocate_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = window->command_pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
        };
        res = vr_vk.vkAllocateCommandBuffers(window->device,
                                             &command_buffer_allocate_info,
                                             &window->command_buffer);

        if (res != VK_SUCCESS) {
                vr_error_message("Error creating command buffer");
                goto error;
        }

        VkDescriptorPoolSize pool_size = {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 4
        };
        VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets = 4,
                .poolSizeCount = 1,
                .pPoolSizes = &pool_size
        };
        res = vr_vk.vkCreateDescriptorPool(window->device,
                                           &descriptor_pool_create_info,
                                           NULL, /* allocator */
                                           &window->descriptor_pool);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating VkDescriptorPool");
                goto error;
        }

        VkAttachmentDescription attachment_descriptions[] = {
                {
                        .format = COLOR_IMAGE_FORMAT,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
        };
        VkSubpassDescription subpass_descriptions[] = {
                {
                        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &(VkAttachmentReference) {
                                .attachment = 0,
                                .layout =
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                        },
                }
        };
        VkRenderPassCreateInfo render_pass_create_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                .attachmentCount = VR_N_ELEMENTS(attachment_descriptions),
                .pAttachments = attachment_descriptions,
                .subpassCount = VR_N_ELEMENTS(subpass_descriptions),
                .pSubpasses = subpass_descriptions
        };
        res = vr_vk.vkCreateRenderPass(window->device,
                                       &render_pass_create_info,
                                       NULL, /* allocator */
                                       &window->render_pass);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating render pass");
                goto error;
        }

        VkFenceCreateInfo fence_create_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        res = vr_vk.vkCreateFence(window->device,
                                  &fence_create_info,
                                  NULL, /* allocator */
                                  &window->vk_fence);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating fence");
                goto error;
        }

        return true;

error:
        deinit_vk(window);
        return false;
}

struct vr_window *
vr_window_new(void)
{
        struct vr_window *window = vr_calloc(sizeof *window);

        if (!vr_vk_load_libvulkan())
                goto error;

        window->libvulkan_loaded = true;

        if (!init_vk(window))
                goto error;

        return window;

error:
        vr_window_free(window);
        return NULL;
}

void
vr_window_free(struct vr_window *window)
{
        deinit_vk(window);

        if (window->libvulkan_loaded)
                vr_vk_unload_libvulkan();

        vr_free(window);
}
