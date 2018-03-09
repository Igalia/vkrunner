/*
 * vkrunner
 *
 * Copyright (C) 2018 Neil Roberts
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

#include "vr-test.h"
#include "vr-list.h"
#include "vr-error-message.h"
#include "vr-allocate-store.h"
#include "vr-flush-memory.h"

#include <math.h>
#include <stdio.h>

struct test_buffer {
        struct vr_list link;
        VkBuffer buffer;
        VkDeviceMemory memory;
        void *memory_map;
        int memory_type_index;
};

struct test_data {
        struct vr_window *window;
        struct vr_pipeline *pipeline;
        struct vr_list buffers;
};

static const float
tolerance[4] = { 0.01f, 0.01f, 0.01f, 0.01f };

static struct test_buffer *
allocate_test_buffer(struct test_data *data,
                     size_t size,
                     VkBufferUsageFlagBits usage)
{
        struct test_buffer *buffer = vr_calloc(sizeof *buffer);
        VkResult res;

        vr_list_insert(data->buffers.prev, &buffer->link);

        VkBufferCreateInfo buffer_create_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                .size = size,
                .usage = usage,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        res = vr_vk.vkCreateBuffer(data->window->device,
                                   &buffer_create_info,
                                   NULL, /* allocator */
                                   &buffer->buffer);
        if (res != VK_SUCCESS) {
                buffer->buffer = NULL;
                vr_error_message("Error creating buffer");
                return NULL;
        }

        res = vr_allocate_store_buffer(data->window,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       1, /* n_buffers */
                                       &buffer->buffer,
                                       &buffer->memory,
                                       &buffer->memory_type_index,
                                       NULL /* offsets */);
        if (res != VK_SUCCESS) {
                buffer->memory = NULL;
                vr_error_message("Error allocating memory");
                return NULL;
        }

        res = vr_vk.vkMapMemory(data->window->device,
                                buffer->memory,
                                0, /* offset */
                                VK_WHOLE_SIZE,
                                0, /* flags */
                                &buffer->memory_map);
        if (res != VK_SUCCESS) {
                buffer->memory_map = NULL;
                vr_error_message("Error mapping memory");
                return NULL;
        }

        return buffer;
}

static void
free_test_buffer(struct test_data *data,
                 struct test_buffer *buffer)
{
        struct vr_window *window = data->window;

        if (buffer->memory_map) {
                vr_vk.vkUnmapMemory(window->device,
                                    buffer->memory);
        }
        if (buffer->memory) {
                vr_vk.vkFreeMemory(window->device,
                                   buffer->memory,
                                   NULL /* allocator */);
        }
        if (buffer->buffer) {
                vr_vk.vkDestroyBuffer(window->device,
                                      buffer->buffer,
                                      NULL /* allocator */);
        }

        vr_list_remove(&buffer->link);
        vr_free(buffer);
}

static bool
begin_paint(struct test_data *data)
{
        VkResult res;

        VkCommandBufferBeginInfo begin_command_buffer_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        res = vr_vk.vkBeginCommandBuffer(data->window->command_buffer,
                                         &begin_command_buffer_info);
        if (res != VK_SUCCESS) {
                vr_error_message("vkBeginCommandBuffer failed");
                return false;
        }

        VkRenderPassBeginInfo render_pass_begin_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = data->window->render_pass,
                .framebuffer = data->window->framebuffer,
                .renderArea = {
                        .offset = { 0, 0 },
                        .extent = {
                                VR_WINDOW_WIDTH, VR_WINDOW_HEIGHT
                        }
                },
        };
        vr_vk.vkCmdBeginRenderPass(data->window->command_buffer,
                                   &render_pass_begin_info,
                                   VK_SUBPASS_CONTENTS_INLINE);

        vr_vk.vkCmdBindPipeline(data->window->command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                data->pipeline->pipeline);

        return true;
}

static bool
end_paint(struct test_data *data)
{
        struct vr_window *window = data->window;
        VkResult res;

        vr_vk.vkCmdEndRenderPass(window->command_buffer);

        VkImageCopy copy_region = {
                .srcSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                },
                .srcOffset = { 0, 0, 0 },
                .dstSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                },
                .dstOffset = { 0, 0, 0 },
                .extent = { VR_WINDOW_WIDTH, VR_WINDOW_HEIGHT, 1 }
        };
        vr_vk.vkCmdCopyImage(window->command_buffer,
                             window->color_image,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             window->linear_image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             1, /* regionCount */
                             &copy_region);

        res = vr_vk.vkEndCommandBuffer(window->command_buffer);
        if (res != VK_SUCCESS) {
                vr_error_message("vkEndCommandBuffer failed");
                return false;
        }

        vr_vk.vkResetFences(window->device,
                            1, /* fenceCount */
                            &window->vk_fence);

        VkSubmitInfo submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount = 1,
                .pCommandBuffers = &window->command_buffer,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = (VkSemaphore[]) { window->vk_semaphore },
                .pWaitDstStageMask =
                (VkPipelineStageFlagBits[])
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT }
        };
        res = vr_vk.vkQueueSubmit(window->queue,
                                  1, /* submitCount */
                                  &submit_info,
                                  window->vk_fence);
        if (res != VK_SUCCESS) {
                vr_error_message("vkQueueSubmit failed");
                return false;
        }

        res = vr_vk.vkWaitForFences(window->device,
                                    1, /* fenceCount */
                                    &window->vk_fence,
                                    VK_TRUE, /* waitAll */
                                    UINT64_MAX);
        if (res != VK_SUCCESS) {
                vr_error_message("vkWaitForFences failed");
                return false;
        }

        if (window->need_linear_memory_invalidate) {
                VkMappedMemoryRange memory_range = {
                        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                        .memory = window->linear_memory,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                };
                vr_vk.vkInvalidateMappedMemoryRanges(window->device,
                                                     1, /* memoryRangeCount */
                                                     &memory_range);
        }

        return true;
}

static bool
draw_rect(struct test_data *data,
          const struct vr_script_command *command)
{
        struct test_buffer *buffer;

        buffer = allocate_test_buffer(data,
                                      sizeof (struct vr_pipeline_vertex) * 6,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (buffer == NULL)
                return false;

        struct vr_pipeline_vertex *v = buffer->memory_map;

        v->x = command->draw_rect.x;
        v->y = command->draw_rect.y;
        v->z = 0.0f;
        v++;

        v->x = command->draw_rect.x + command->draw_rect.w;
        v->y = command->draw_rect.y;
        v->z = 0.0f;
        v++;

        v->x = command->draw_rect.x;
        v->y = command->draw_rect.y + command->draw_rect.h;
        v->z = 0.0f;
        v++;

        v->x = command->draw_rect.x;
        v->y = command->draw_rect.y + command->draw_rect.h;
        v->z = 0.0f;
        v++;

        v->x = command->draw_rect.x + command->draw_rect.w;
        v->y = command->draw_rect.y;
        v->z = 0.0f;
        v++;

        v->x = command->draw_rect.x + command->draw_rect.w;
        v->y = command->draw_rect.y + command->draw_rect.h;
        v->z = 0.0f;
        v++;

        vr_flush_memory(data->window,
                        buffer->memory_type_index,
                        buffer->memory,
                        VK_WHOLE_SIZE);

        vr_vk.vkCmdBindVertexBuffers(data->window->command_buffer,
                                     0, /* firstBinding */
                                     1, /* bindingCount */
                                     &buffer->buffer,
                                     (VkDeviceSize[]) { 0 });
        vr_vk.vkCmdDraw(data->window->command_buffer,
                        6, /* vertexCount */
                        1, /* instanceCount */
                        0, /* firstVertex */
                        0 /* firstInstance */);

        return true;
}

static bool
compare_pixels(const float *color1,
               const float *color2,
               const float *tolerance)
{
        for (int p = 0; p < 4; ++p)
                if (fabsf(color1[p] - color2[p]) > tolerance[p])
                        return false;
        return true;
}

static void
print_components_float(const float *pixel)
{
        int p;
        for (p = 0; p < 4; ++p)
                printf(" %f", pixel[p]);
}

static void
print_bad_pixel(int x, int y,
                const float *expected,
                const float *observed)
{
        printf("Probe color at (%i,%i)\n"
               "  Expected:",
               x, y);
        print_components_float(expected);
        printf("\n"
               "  Observed:");
        print_components_float(observed);
        printf("\n");
}

static void
load_pixel(const uint8_t *fb,
           float *pixel)
{
        for (int i = 0; i < 3; i++)
                pixel[2 - i] = fb[i] / 255.0f;
        pixel[3] = fb[3] / 255.0f;
}

static bool
probe_rect_rgba(struct test_data *data,
                const struct vr_script_command *command)
{
        bool ret = true;

        /* End the paint to copy the framebuffer into the linear buffer */
        if (!end_paint(data))
                ret = false;

        for (int y = 0; y < command->probe_rect.h; y++) {
                const uint8_t *p =
                        ((y + command->probe_rect.y) *
                         data->window->linear_memory_stride +
                         command->probe_rect.x * 4 +
                         (uint8_t *) data->window->linear_memory_map);
                for (int x = 0; x < command->probe_rect.w; x++) {
                        float pixel[4];
                        load_pixel(p, pixel);
                        p += 4;

                        if (!compare_pixels(pixel,
                                            command->probe_rect.color,
                                            tolerance)) {
                                ret = false;
                                print_bad_pixel(x + command->probe_rect.x,
                                                y + command->probe_rect.y,
                                                command->probe_rect.color,
                                                pixel);
                                goto done;
                        }
                }
        }
done:

        if (!begin_paint(data))
                ret = false;

        return ret;
}

bool
vr_test_run(struct vr_window *window,
            struct vr_pipeline *pipeline,
            const struct vr_script *script)
{
        struct test_data data = {
                .window = window,
                .pipeline = pipeline
        };
        bool ret = true;

        vr_list_init(&data.buffers);

        if (!begin_paint(&data))
                ret = false;

        for (int i = 0; i < script->n_commands; i++) {
                const struct vr_script_command *command = script->commands + i;

                switch (command->op) {
                case VR_SCRIPT_OP_DRAW_RECT:
                        if (!draw_rect(&data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_PROBE_RECT_RGBA:
                        if (!probe_rect_rgba(&data, command))
                                ret = false;
                        break;
                }
        }

        if (!end_paint(&data))
                ret = false;

        struct test_buffer *buffer, *tmp;
        vr_list_for_each_safe(buffer, tmp, &data.buffers, link) {
                free_test_buffer(&data, buffer);
        }

        return ret;
}
