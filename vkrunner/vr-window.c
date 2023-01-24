/*
 * vkrunner
 *
 * Copyright (C) 2013, 2014, 2015, 2017 Neil Roberts
 * Copyright (C) 2019 Google LLC
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
#include "vr-format-private.h"

struct vr_window {
        struct vr_context *context;

        const struct vr_config *config;

        /* The first render pass is used for the first render and has
         * a loadOp of DONT_CARE. The second is used for subsequent
         * renders and loads the framebuffer contents.
         */
        VkRenderPass render_pass[2];

        VkImage color_image;
        VkBuffer linear_buffer;
        VkDeviceMemory memory;
        VkDeviceMemory linear_memory;
        bool need_linear_memory_invalidate;
        void *linear_memory_map;
        VkDeviceSize linear_memory_stride;
        VkImageView color_image_view;
        VkImage depth_image;
        VkDeviceMemory depth_image_memory;
        VkImageView depth_image_view;
        VkFramebuffer framebuffer;
        struct vr_window_format format;
};

static void
destroy_framebuffer_resources(struct vr_window *window)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkDevice device = vr_window_get_device(window);

        if (window->color_image_view) {
                vkfn->vkDestroyImageView(device,
                                         window->color_image_view,
                                         NULL /* allocator */);
                window->color_image_view = VK_NULL_HANDLE;
        }
        if (window->framebuffer) {
                vkfn->vkDestroyFramebuffer(device,
                                           window->framebuffer,
                                           NULL /* allocator */);
                window->framebuffer = VK_NULL_HANDLE;
        }
        if (window->linear_memory_map) {
                vkfn->vkUnmapMemory(device,
                                    window->linear_memory);
                window->linear_memory_map = NULL;
        }
        if (window->linear_memory) {
                vkfn->vkFreeMemory(device,
                                   window->linear_memory,
                                   NULL /* allocator */);
                window->linear_memory = VK_NULL_HANDLE;
        }
        if (window->memory) {
                vkfn->vkFreeMemory(device,
                                   window->memory,
                                   NULL /* allocator */);
                window->memory = VK_NULL_HANDLE;
        }
        if (window->linear_buffer) {
                vkfn->vkDestroyBuffer(device,
                                      window->linear_buffer,
                                      NULL /* allocator */);
                window->linear_buffer = VK_NULL_HANDLE;
        }
        if (window->color_image) {
                vkfn->vkDestroyImage(device,
                                     window->color_image,
                                     NULL /* allocator */);
                window->color_image = VK_NULL_HANDLE;
        }
        if (window->depth_image_view) {
                vkfn->vkDestroyImageView(device,
                                         window->depth_image_view,
                                         NULL /* allocator */);
                window->depth_image_view = VK_NULL_HANDLE;
        }
        if (window->depth_image_memory) {
                vkfn->vkFreeMemory(device,
                                   window->depth_image_memory,
                                   NULL /* allocator */);
                window->depth_image_memory = VK_NULL_HANDLE;
        }
        if (window->depth_image) {
                vkfn->vkDestroyImage(device,
                                     window->depth_image,
                                     NULL /* allocator */);
                window->depth_image = VK_NULL_HANDLE;
        }
        for (int i = 0; i < VR_N_ELEMENTS(window->render_pass); i++) {
                if (window->render_pass[i]) {
                        vkfn->vkDestroyRenderPass(device,
                                                  window->render_pass[i],
                                                  NULL /* allocator */);
                        window->render_pass[i] = VK_NULL_HANDLE;
                }
        }
}

static bool
init_depth_stencil_resources(struct vr_window *window)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkResult res;

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = window->format.depth_stencil_format->vk_format,
                .extent = {
                        .width = window->format.width,
                        .height = window->format.height,
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
        res = vkfn->vkCreateImage(vr_window_get_device(window),
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &window->depth_image);
        if (res != VK_SUCCESS) {
                window->depth_image = VK_NULL_HANDLE;
                vr_error_message(window->config,
                                 "Error creating depth/stencil image");
                return false;
        }

        res = vr_allocate_store_image(window->context,
                                      0, /* memory_type_flags */
                                      1, /* n_images */
                                      &window->depth_image,
                                      &window->depth_image_memory,
                                      NULL /* memory_type_index */);
        if (res != VK_SUCCESS) {
                vr_error_message(window->config,
                                 "Error allocating depth/stencil memory");
                return false;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = window->depth_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = window->format.depth_stencil_format->vk_format,
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
        res = vkfn->vkCreateImageView(vr_window_get_device(window),
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &window->depth_image_view);
        if (res != VK_SUCCESS) {
                window->depth_image_view = VK_NULL_HANDLE;
                vr_error_message(window->config,
                                 "Error creating depth/stencil image view");
                return false;
        }

        return true;
}

static bool
check_format(struct vr_window *window,
             const struct vr_format *format,
             VkFormatFeatureFlags flags)
{
        const struct vr_vk_instance *vkfn =
                vr_context_get_vkinst(window->context);
        VkFormatProperties format_properties;
        VkPhysicalDevice physical_device =
                vr_context_get_physical_device(window->context);

        vkfn->vkGetPhysicalDeviceFormatProperties(physical_device,
                                                  format->vk_format,
                                                  &format_properties);

        return (format_properties.optimalTilingFeatures & flags) == flags;
}

static bool
create_render_pass(struct vr_window *window,
                   bool first_render,
                   VkRenderPass *render_pass_out)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkResult res;
        bool has_stencil = false;
        const struct vr_format *depth_stencil_format =
                window->format.depth_stencil_format;

        if (depth_stencil_format) {
                for (int i = 0; i < depth_stencil_format->n_parts; i++) {
                        if (depth_stencil_format->parts[i].component ==
                            VR_FORMAT_COMPONENT_S)
                                has_stencil = true;
                }
        }

        VkAttachmentDescription attachment_descriptions[] = {
                {
                        .format = window->format.color_format->vk_format,
                        .samples = VK_SAMPLE_COUNT_1_BIT,
                        .loadOp = (first_render ?
                                   VK_ATTACHMENT_LOAD_OP_DONT_CARE :
                                   VK_ATTACHMENT_LOAD_OP_LOAD),
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                        .initialLayout =
                        (first_render ?
                         VK_IMAGE_LAYOUT_UNDEFINED :
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
                        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                },
                {
                        .format = (depth_stencil_format ?
                                   depth_stencil_format->vk_format :
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
        if (depth_stencil_format == NULL) {
                render_pass_create_info.attachmentCount--;
                subpass_descriptions[0].pDepthStencilAttachment = NULL;
        }
        res = vkfn->vkCreateRenderPass(vr_window_get_device(window),
                                       &render_pass_create_info,
                                       NULL, /* allocator */
                                       render_pass_out);
        if (res != VK_SUCCESS) {
                *render_pass_out = VK_NULL_HANDLE;
                vr_error_message(window->config, "Error creating render pass");
                return false;
        }

        return true;
}

static bool
init_framebuffer_resources(struct vr_window *window)
{
        const struct vr_vk_device *vkfn = vr_window_get_vkdev(window);
        VkResult res;
        int linear_memory_type;

        if (!create_render_pass(window,
                                true, /* first_render */
                                window->render_pass + 0))
                return false;
        if (!create_render_pass(window,
                                false, /* first_render */
                                window->render_pass + 1))
                return false;

        VkImageCreateInfo image_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = window->format.color_format->vk_format,
                .extent = {
                        .width = window->format.width,
                        .height = window->format.height,
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
        res = vkfn->vkCreateImage(vr_window_get_device(window),
                                  &image_create_info,
                                  NULL, /* allocator */
                                  &window->color_image);
        if (res != VK_SUCCESS) {
                vr_error_message(window->config, "Error creating VkImage");
                return false;
        }

        res = vr_allocate_store_image(window->context,
                                      0, /* memory_type_flags */
                                      1, /* n_images */
                                      (VkImage[]) { window->color_image },
                                      &window->memory,
                                      NULL /* memory_type_index */);

        int format_size = vr_format_get_size(window->format.color_format);
        window->linear_memory_stride = format_size * window->format.width;

        struct VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = window->linear_memory_stride * window->format.height,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = vkfn->vkCreateBuffer(vr_window_get_device(window),
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &window->linear_buffer);
        if (res != VK_SUCCESS) {
                window->linear_buffer = VK_NULL_HANDLE;
                vr_error_message(window->config,
                                 "Error creating linear buffer");
                return false;
        }

        res = vr_allocate_store_buffer(window->context,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &window->linear_buffer,
                                       &window->linear_memory,
                                       &linear_memory_type,
                                       NULL /* offsets */);
        if (res != VK_SUCCESS) {
                vr_error_message(window->config,
                                 "Error allocating linear buffer memory");
                return false;
        }

        const struct VkPhysicalDeviceMemoryProperties *memory_properties =
                vr_context_get_memory_properties(window->context);

        window->need_linear_memory_invalidate =
                (memory_properties->
                 memoryTypes[linear_memory_type].propertyFlags &
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0;

        res = vkfn->vkMapMemory(vr_window_get_device(window),
                                window->linear_memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                &window->linear_memory_map);
        if (res != VK_SUCCESS) {
                vr_error_message(window->config,
                                 "Error mapping linear memory");
                return false;
        }

        VkImageViewCreateInfo image_view_create_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = window->color_image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = window->format.color_format->vk_format,
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
        res = vkfn->vkCreateImageView(vr_window_get_device(window),
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &window->color_image_view);
        if (res != VK_SUCCESS) {
                vr_error_message(window->config,
                                 "Error creating image view");
                return false;
        }

        if (window->format.depth_stencil_format &&
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
                .width = window->format.width,
                .height = window->format.height,
                .layers = 1
        };
        res = vkfn->vkCreateFramebuffer(vr_window_get_device(window),
                                        &framebuffer_create_info,
                                        NULL, /* allocator */
                                        &window->framebuffer);
        if (res != VK_SUCCESS) {
                window->framebuffer = VK_NULL_HANDLE;
                vr_error_message(window->config,
                                 "Error creating framebuffer");
                return false;
        }

        return true;
}

enum vr_result
vr_window_new(const struct vr_config *config,
              struct vr_context *context,
              const struct vr_window_format *format,
              struct vr_window **window_out)
{
        struct vr_window *window = vr_calloc(sizeof *window);
        enum vr_result vres;

        window->config = config;

        window->context = context;

        window->format = *format;

        if (!check_format(window,
                          format->color_format,
                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                          VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
                char *format_name = vr_format_get_name(format->color_format);
                vr_error_message(window->config,
                                 "Format %s is not supported as a color "
                                 "attachment and blit source",
                                 format_name);
                vr_free(format_name);
                vres = VR_RESULT_SKIP;
                goto error;
        }

        if (format->depth_stencil_format &&
            !check_format(window,
                          format->depth_stencil_format,
                          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
                char *format_name =
                        vr_format_get_name(format->depth_stencil_format);
                vr_error_message(window->config,
                                 "Format %s is not supported as a "
                                 "depth/stencil attachment",
                                 format_name);
                vr_free(format_name);
                vres = VR_RESULT_SKIP;
                goto error;
        }

        if (!init_framebuffer_resources(window)) {
                vres = VR_RESULT_FAIL;
                goto error;
        }

        *window_out = window;
        return VR_RESULT_PASS;

error:
        vr_window_free(window);
        return vres;
}

struct vr_context *
vr_window_get_context(struct vr_window *window)
{
        return window->context;
}

const struct vr_window_format *
vr_window_get_format(const struct vr_window *window)
{
        return &window->format;
}

const struct vr_vk_device *
vr_window_get_vkdev(const struct vr_window *window)
{
        return vr_context_get_vkdev(window->context);
}

VkDevice
vr_window_get_device(const struct vr_window *window)
{
        return vr_context_get_vk_device(window->context);
}

VkRenderPass
vr_window_get_render_pass(const struct vr_window *window,
                          bool first_render_pass)
{
        return window->render_pass[first_render_pass ? 0 : 1];
}

const struct vr_config *
vr_window_get_config(const struct vr_window *window)
{
        return window->config;
}

bool
vr_window_need_linear_memory_invalidate(const struct vr_window *window)
{
        return window->need_linear_memory_invalidate;
}

VkDeviceMemory
vr_window_get_linear_memory(const struct vr_window *window)
{
        return window->linear_memory;
}

VkDeviceSize
vr_window_get_linear_memory_stride(const struct vr_window *window)
{
        return window->linear_memory_stride;
}

const void *
vr_window_get_linear_memory_map(struct vr_window *window)
{
        return window->linear_memory_map;
}

VkBuffer
vr_window_get_linear_buffer(const struct vr_window *window)
{
        return window->linear_buffer;
}

VkFramebuffer
vr_window_get_framebuffer(const struct vr_window *window)
{
        return window->framebuffer;
}

VkImage
vr_window_get_color_image(const struct vr_window *window)
{
        return window->color_image;
}

void
vr_window_free(struct vr_window *window)
{
        destroy_framebuffer_resources(window);

        vr_free(window);
}
