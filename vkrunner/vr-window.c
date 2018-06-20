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
#include <string.h>

#include "vr-window.h"
#include "vr-util.h"
#include "vr-error-message.h"
#include "vr-allocate-store.h"
#include "vr-feature-offsets.h"

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
destroy_framebuffer_resources(struct vr_window *window)
{
        if (window->color_image_view) {
                vr_vk.vkDestroyImageView(window->device,
                                         window->color_image_view,
                                         NULL /* allocator */);
                window->color_image_view = NULL;
        }
        if (window->framebuffer) {
                vr_vk.vkDestroyFramebuffer(window->device,
                                           window->framebuffer,
                                           NULL /* allocator */);
                window->framebuffer = NULL;
        }
        if (window->linear_memory_map) {
                vr_vk.vkUnmapMemory(window->device,
                                    window->linear_memory);
                window->linear_memory_map = NULL;
        }
        if (window->linear_memory) {
                vr_vk.vkFreeMemory(window->device,
                                   window->linear_memory,
                                   NULL /* allocator */);
                window->linear_memory = NULL;
        }
        if (window->memory) {
                vr_vk.vkFreeMemory(window->device,
                                   window->memory,
                                   NULL /* allocator */);
                window->memory = NULL;
        }
        if (window->linear_buffer) {
                vr_vk.vkDestroyBuffer(window->device,
                                      window->linear_buffer,
                                      NULL /* allocator */);
                window->linear_buffer = NULL;
        }
        if (window->color_image) {
                vr_vk.vkDestroyImage(window->device,
                                     window->color_image,
                                     NULL /* allocator */);
                window->color_image = NULL;
        }
        if (window->depth_image_view) {
                vr_vk.vkDestroyImageView(window->device,
                                         window->depth_image_view,
                                         NULL /* allocator */);
                window->depth_image_view = NULL;
        }
        if (window->depth_image_memory) {
                vr_vk.vkFreeMemory(window->device,
                                   window->depth_image_memory,
                                   NULL /* allocator */);
                window->depth_image_memory = NULL;
        }
        if (window->depth_image) {
                vr_vk.vkDestroyImage(window->device,
                                     window->depth_image,
                                     NULL /* allocator */);
                window->depth_image = NULL;
        }
}

static bool
init_depth_stencil_resources(struct vr_window *window)
{
        VkResult res;

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = window->depth_stencil_format->vk_format,
                .extent = {
                        .width = VR_WINDOW_WIDTH,
                        .height = VR_WINDOW_HEIGHT,
                        .depth = 1
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        res = vr_vk.vkCreateImage(window->device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &window->depth_image);
        if (res != VK_SUCCESS) {
                window->depth_image = NULL;
                vr_error_message("Error creating depth/stencil image");
                return false;
        }

        res = vr_allocate_store_image(window,
                                      0, /* memory_type_flags */
                                      1, /* n_images */
                                      &window->depth_image,
                                      &window->depth_image_memory,
                                      NULL /* memory_type_index */);
        if (res != VK_SUCCESS) {
                vr_error_message("Error allocating depth/stencil memory");
                return false;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = window->depth_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = window->depth_stencil_format->vk_format,
                .components = {
                        .r = VK_COMPONENT_SWIZZLE_R,
                        .g = VK_COMPONENT_SWIZZLE_G,
                        .b = VK_COMPONENT_SWIZZLE_B,
                        .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = vr_vk.vkCreateImageView(window->device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &window->depth_image_view);
        if (res != VK_SUCCESS) {
                window->depth_image_view = NULL;
                vr_error_message("Error creating depth/stencil image view");
                return false;
        }

        return true;
}

static bool
init_framebuffer_resources(struct vr_window *window)
{
        VkResult res;
        int linear_memory_type;

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = window->framebuffer_format->vk_format,
                .extent = {
                        .width = VR_WINDOW_WIDTH,
                        .height = VR_WINDOW_HEIGHT,
                        .depth = 1
                },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        res = vr_vk.vkCreateImage(window->device,
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &window->color_image);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating VkImage");
                return false;
        }

        res = vr_allocate_store_image(window,
                                      0, /* memory_type_flags */
                                      1, /* n_images */
                                      (VkImage[]) { window->color_image },
                                      &window->memory,
                                      NULL /* memory_type_index */);

        int format_size = vr_format_get_size(window->framebuffer_format);
        window->linear_memory_stride = format_size * VR_WINDOW_WIDTH;

        struct VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = window->linear_memory_stride * VR_WINDOW_HEIGHT,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = vr_vk.vkCreateBuffer(window->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &window->linear_buffer);
        if (res != VK_SUCCESS) {
                window->linear_buffer = NULL;
                vr_error_message("Error creating linear buffer");
                return false;
        }

        res = vr_allocate_store_buffer(window,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &window->linear_buffer,
                                       &window->linear_memory,
                                       &linear_memory_type,
                                       NULL /* offsets */);
        if (res != VK_SUCCESS) {
                vr_error_message("Error allocating linear buffer memory");
                return false;
        }

        window->need_linear_memory_invalidate =
                (window->memory_properties.
                 memoryTypes[linear_memory_type].propertyFlags &
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0;

        res = vr_vk.vkMapMemory(window->device,
                                window->linear_memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                &window->linear_memory_map);
        if (res != VK_SUCCESS) {
                vr_error_message("Error mapping linear memory");
                return false;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = window->color_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = window->framebuffer_format->vk_format,
                .components = {
                        .r = VK_COMPONENT_SWIZZLE_R,
                        .g = VK_COMPONENT_SWIZZLE_G,
                        .b = VK_COMPONENT_SWIZZLE_B,
                        .a = VK_COMPONENT_SWIZZLE_A
                },
                .subresourceRange = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                }
        };
        res = vr_vk.vkCreateImageView(window->device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &window->color_image_view);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating image view");
                return false;
        }

        if (window->depth_stencil_format &&
            !init_depth_stencil_resources(window))
                return false;

        VkImageView attachments[] = {
                window->color_image_view,
                window->depth_image_view,
        };
        VkFramebufferCreateInfo framebuffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = window->render_pass[0],
                .attachmentCount = (window->depth_image_view ?
                                    VR_N_ELEMENTS(attachments) :
                                    VR_N_ELEMENTS(attachments) - 1),
                .pAttachments = attachments,
                .width = VR_WINDOW_WIDTH,
                .height = VR_WINDOW_HEIGHT,
                .layers = 1
        };
        res = vr_vk.vkCreateFramebuffer(window->device,
                                        &framebuffer_create_info,
                                        NULL, /* allocator */
                                        &window->framebuffer);
        if (res != VK_SUCCESS) {
                window->framebuffer = NULL;
                vr_error_message("Error creating framebuffer");
                return false;
        }

        return true;
}

static void
deinit_vk(struct vr_window *window)
{
        destroy_framebuffer_resources(window);

        if (window->vk_fence) {
                vr_vk.vkDestroyFence(window->device,
                                     window->vk_fence,
                                     NULL /* allocator */);
                window->vk_fence = NULL;
        }
        for (int i = 0; i < VR_N_ELEMENTS(window->render_pass); i++) {
                if (window->render_pass[i]) {
                        vr_vk.vkDestroyRenderPass(window->device,
                                                  window->render_pass[i],
                                                  NULL /* allocator */);
                        window->render_pass[i] = NULL;
                }
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
check_extensions(VkPhysicalDevice device,
                 const char *const *extensions)
{
        VkResult res;
        uint32_t property_count;

        res = vr_vk.vkEnumerateDeviceExtensionProperties(device,
                                                         NULL, /* layerName */
                                                         &property_count,
                                                         NULL /* properties */);
        if (res != VK_SUCCESS)
                return false;

        VkExtensionProperties *props = vr_alloc(property_count * sizeof *props);
        bool ret = true;

        res = vr_vk.vkEnumerateDeviceExtensionProperties(device,
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
find_physical_device(struct vr_window *window,
                     const VkPhysicalDeviceFeatures *requires,
                     const char *const *extensions)

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
                return VR_RESULT_FAIL;
        }

        devices = alloca(count * sizeof *devices);

        res = vr_vk.vkEnumeratePhysicalDevices(window->vk_instance,
                                               &count,
                                               devices);
        if (res != VK_SUCCESS) {
                vr_error_message("Error enumerating VkPhysicalDevices");
                return VR_RESULT_FAIL;
        }

        for (i = 0; i < count; i++) {
                VkPhysicalDeviceFeatures features;
                vr_vk.vkGetPhysicalDeviceFeatures(devices[i], &features);
                if (!check_features(&features, requires))
                        continue;

                if (!check_extensions(devices[i], extensions))
                        continue;

                queue_family = find_queue_family(window, devices[i]);
                if (queue_family == -1)
                        continue;

                window->physical_device = devices[i];
                window->queue_family = queue_family;

                return VR_RESULT_PASS;
        }

        vr_error_message("No suitable device and queue family found");

        return VR_RESULT_SKIP;
}

static bool
create_render_pass(struct vr_window *window,
                   bool first_render,
                   VkRenderPass *render_pass_out)
{
        VkResult res;
        bool has_stencil = false;

        if (window->depth_stencil_format) {
                const struct vr_format *format = window->depth_stencil_format;

                for (int i = 0; i < format->n_parts; i++) {
                        if (format->parts[i].component == VR_FORMAT_COMPONENT_S)
                                has_stencil = true;
                }
        }

        VkAttachmentDescription attachment_descriptions[] = {
                {
                        .format = window->framebuffer_format->vk_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = (first_render ?
                                   VK_ATTACHMENT_LOAD_OP_DONT_CARE :
                                   VK_ATTACHMENT_LOAD_OP_LOAD),
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout = (first_render ?
                                          VK_IMAGE_LAYOUT_UNDEFINED :
                                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
                        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                },
                {
                        .format = (window->depth_stencil_format ?
                                   window->depth_stencil_format->vk_format :
                                   0),
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = (first_render ?
                                   VK_ATTACHMENT_LOAD_OP_DONT_CARE :
                                   VK_ATTACHMENT_LOAD_OP_LOAD),
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = (first_render || !has_stencil ?
                                          VK_ATTACHMENT_LOAD_OP_DONT_CARE :
                                          VK_ATTACHMENT_LOAD_OP_LOAD),
                        .stencilStoreOp = (has_stencil ?
                                           VK_ATTACHMENT_STORE_OP_STORE :
                                           VK_ATTACHMENT_STORE_OP_DONT_CARE),
                        .initialLayout =
                        (first_render ?
                         VK_IMAGE_LAYOUT_UNDEFINED :
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL),
                        .finalLayout =
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
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
                        .pDepthStencilAttachment = &(VkAttachmentReference) {
                                .attachment = 1,
                                .layout =
                                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
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
        if (window->depth_stencil_format == NULL) {
                render_pass_create_info.attachmentCount--;
                subpass_descriptions[0].pDepthStencilAttachment = NULL;
        }
        res = vr_vk.vkCreateRenderPass(window->device,
                                       &render_pass_create_info,
                                       NULL, /* allocator */
                                       render_pass_out);
        if (res != VK_SUCCESS) {
                *render_pass_out = NULL;
                vr_error_message("Error creating render pass");
                return false;
        }

        return true;
}

static bool
check_format(struct vr_window *window,
             const struct vr_format *format,
             VkFormatFeatureFlags flags)
{
        VkFormatProperties format_properties;

        vr_vk.vkGetPhysicalDeviceFormatProperties(window->physical_device,
                                                  format->vk_format,
                                                  &format_properties);

        return (format_properties.optimalTilingFeatures & flags) == flags;
}

static enum vr_result
init_vk(struct vr_window *window,
        const VkPhysicalDeviceFeatures *requires,
        const char *const *extensions)
{
        VkPhysicalDeviceMemoryProperties *memory_properties =
                &window->memory_properties;
        enum vr_result vres = VR_RESULT_PASS;
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
                vres = VR_RESULT_FAIL;
                goto error;
        }

        vr_vk_init_instance(window->vk_instance);

        vres = find_physical_device(window, requires, extensions);
        if (vres != VR_RESULT_PASS)
                goto error;

        vr_vk.vkGetPhysicalDeviceProperties(window->physical_device,
                                            &window->device_properties);
        vr_vk.vkGetPhysicalDeviceMemoryProperties(window->physical_device,
                                                  memory_properties);
        vr_vk.vkGetPhysicalDeviceFeatures(window->physical_device,
                                          &window->features);

        if (!check_format(window,
                          window->framebuffer_format,
                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                          VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
                vr_error_message("Format %s is not supported as a color "
                                 "attachment and blit source",
                                 window->framebuffer_format->name);
                vres = VR_RESULT_FAIL;
                goto error;
        }

        if (window->depth_stencil_format &&
            !check_format(window,
                          window->depth_stencil_format,
                          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
                vr_error_message("Format %s is not supported as a depth/stencil "
                                 "attachment",
                                 window->depth_stencil_format->name);
                return false;
        }

        int n_extensions = 0;
        for (const char * const *ext = extensions; *ext; ext++)
                n_extensions++;

        VkDeviceCreateInfo device_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                .queueCreateInfoCount = 1,
                .pQueueCreateInfos = &(VkDeviceQueueCreateInfo) {
                        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                        .queueFamilyIndex = window->queue_family,
                        .queueCount = 1,
                        .pQueuePriorities = (float[]) { 1.0f }
                },
                .enabledExtensionCount = n_extensions,
                .ppEnabledExtensionNames = n_extensions ? extensions : NULL,
                .pEnabledFeatures = requires
        };
        res = vr_vk.vkCreateDevice(window->physical_device,
                                   &device_create_info,
                                   NULL, /* allocator */
                                   &window->device);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating VkDevice");
                vres = VR_RESULT_FAIL;
                goto error;
        }

        vr_vk_init_device(window->device);

        vr_vk.vkGetDeviceQueue(window->device,
                               window->queue_family,
                               0, /* queueIndex */
                               &window->queue);

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
                vres = VR_RESULT_FAIL;
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
        res = vr_vk.vkCreateDescriptorPool(window->device,
                                           &descriptor_pool_create_info,
                                           NULL, /* allocator */
                                           &window->descriptor_pool);
        if (res != VK_SUCCESS) {
                vr_error_message("Error creating VkDescriptorPool");
                vres = VR_RESULT_FAIL;
                goto error;
        }

        if (!create_render_pass(window,
                                true, /* first_render */
                                window->render_pass + 0)) {
                vres = VR_RESULT_FAIL;
                goto error;
        }
        if (!create_render_pass(window,
                                false, /* first_render */
                                window->render_pass + 1)) {
                vres = VR_RESULT_FAIL;
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
                vres = VR_RESULT_FAIL;
                goto error;
        }

        if (!init_framebuffer_resources(window)) {
                vres = VR_RESULT_FAIL;
                goto error;
        }

        return VR_RESULT_PASS;

error:
        deinit_vk(window);
        return vres;
}

enum vr_result
vr_window_new(const VkPhysicalDeviceFeatures *requires,
              const char *const *extensions,
              const struct vr_format *framebuffer_format,
              const struct vr_format *depth_stencil_format,
              struct vr_window **window_out)
{
        struct vr_window *window = vr_calloc(sizeof *window);
        enum vr_result vres;

        if (!vr_vk_load_libvulkan()) {
                vres = VR_RESULT_FAIL;
                goto error;
        }

        window->libvulkan_loaded = true;
        window->framebuffer_format = framebuffer_format;
        window->depth_stencil_format = depth_stencil_format;

        vres = init_vk(window, requires, extensions);
        if (vres != VR_RESULT_PASS)
                goto error;

        *window_out = window;
        return VR_RESULT_PASS;

error:
        vr_window_free(window);
        return vres;
}

void
vr_window_free(struct vr_window *window)
{
        deinit_vk(window);

        if (window->libvulkan_loaded)
                vr_vk_unload_libvulkan();

        vr_free(window);
}
