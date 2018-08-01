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

static void
destroy_framebuffer_resources(struct vr_window *window)
{
        struct vr_vk *vkfn = &window->vkfn;

        if (window->color_image_view) {
                vkfn->vkDestroyImageView(window->device,
                                         window->color_image_view,
                                         NULL /* allocator */);
                window->color_image_view = VK_NULL_HANDLE;
        }
        if (window->framebuffer) {
                vkfn->vkDestroyFramebuffer(window->device,
                                           window->framebuffer,
                                           NULL /* allocator */);
                window->framebuffer = VK_NULL_HANDLE;
        }
        if (window->linear_memory_map) {
                vkfn->vkUnmapMemory(window->device,
                                    window->linear_memory);
                window->linear_memory_map = NULL;
        }
        if (window->linear_memory) {
                vkfn->vkFreeMemory(window->device,
                                   window->linear_memory,
                                   NULL /* allocator */);
                window->linear_memory = VK_NULL_HANDLE;
        }
        if (window->memory) {
                vkfn->vkFreeMemory(window->device,
                                   window->memory,
                                   NULL /* allocator */);
                window->memory = VK_NULL_HANDLE;
        }
        if (window->linear_buffer) {
                vkfn->vkDestroyBuffer(window->device,
                                      window->linear_buffer,
                                      NULL /* allocator */);
                window->linear_buffer = VK_NULL_HANDLE;
        }
        if (window->color_image) {
                vkfn->vkDestroyImage(window->device,
                                     window->color_image,
                                     NULL /* allocator */);
                window->color_image = VK_NULL_HANDLE;
        }
        if (window->depth_image_view) {
                vkfn->vkDestroyImageView(window->device,
                                         window->depth_image_view,
                                         NULL /* allocator */);
                window->depth_image_view = VK_NULL_HANDLE;
        }
        if (window->depth_image_memory) {
                vkfn->vkFreeMemory(window->device,
                                   window->depth_image_memory,
                                   NULL /* allocator */);
                window->depth_image_memory = VK_NULL_HANDLE;
        }
        if (window->depth_image) {
                vkfn->vkDestroyImage(window->device,
                                     window->depth_image,
                                     NULL /* allocator */);
                window->depth_image = VK_NULL_HANDLE;
        }
        for (int i = 0; i < VR_N_ELEMENTS(window->render_pass); i++) {
                if (window->render_pass[i]) {
                        vkfn->vkDestroyRenderPass(window->device,
                                                  window->render_pass[i],
                                                  NULL /* allocator */);
                        window->render_pass[i] = VK_NULL_HANDLE;
                }
        }
}

static bool
init_depth_stencil_resources(struct vr_window *window)
{
        struct vr_vk *vkfn = &window->vkfn;
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
        res = vkfn->vkCreateImage(window->device,
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
        res = vkfn->vkCreateImageView(window->device,
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
        struct vr_vk *vkfn = &window->vkfn;
        VkFormatProperties format_properties;
        VkPhysicalDevice physical_device = window->context->physical_device;

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
        struct vr_vk *vkfn = &window->vkfn;
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
                        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
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
        res = vkfn->vkCreateRenderPass(window->device,
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
        struct vr_vk *vkfn = &window->vkfn;
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
        res = vkfn->vkCreateImage(window->device,
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

        int format_size = vr_format_get_size(window->framebuffer_format);
        window->linear_memory_stride = format_size * VR_WINDOW_WIDTH;

        struct VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = window->linear_memory_stride * VR_WINDOW_HEIGHT,
                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = vkfn->vkCreateBuffer(window->device,
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

        window->need_linear_memory_invalidate =
                (window->context->memory_properties.
                 memoryTypes[linear_memory_type].propertyFlags &
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0;

        res = vkfn->vkMapMemory(window->device,
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
        res = vkfn->vkCreateImageView(window->device,
                                      &image_view_create_info,
                                      NULL, /* allocator */
                                      &window->color_image_view);
        if (res != VK_SUCCESS) {
                vr_error_message(window->config,
                                 "Error creating image view");
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
        res = vkfn->vkCreateFramebuffer(window->device,
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
vr_window_new(struct vr_context *context,
              const struct vr_format *framebuffer_format,
              const struct vr_format *depth_stencil_format,
              struct vr_window **window_out)
{
        struct vr_window *window = vr_calloc(sizeof *window);
        struct vr_vk *vkfn = &window->vkfn;
        enum vr_result vres;

        window->context = context;
        window->config = context->config;
        window->device = context->device;
        *vkfn = context->vkfn;

        window->framebuffer_format = framebuffer_format;
        window->depth_stencil_format = depth_stencil_format;

        if (!check_format(window,
                          framebuffer_format,
                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                          VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
                vr_error_message(window->config,
                                 "Format %s is not supported as a color "
                                 "attachment and blit source",
                                 window->framebuffer_format->name);
                vres = VR_RESULT_SKIP;
                goto error;
        }

        if (window->depth_stencil_format &&
            !check_format(window,
                          depth_stencil_format,
                          VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
                vr_error_message(window->config,
                                 "Format %s is not supported as a "
                                 "depth/stencil attachment",
                                 window->depth_stencil_format->name);
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

void
vr_window_free(struct vr_window *window)
{
        destroy_framebuffer_resources(window);

        vr_free(window);
}
