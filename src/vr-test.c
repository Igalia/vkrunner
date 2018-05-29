/*
 * vkrunner
 *
 * Copyright (C) 2018 Neil Roberts
 * Copyright (C) 2018 Intel Coporation
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
#include <string.h>
#include <assert.h>

struct test_buffer {
        struct vr_list link;
        VkBuffer buffer;
        VkDeviceMemory memory;
        void *memory_map;
        int memory_type_index;
        size_t size;
};

enum test_state {
        /* Any rendering or computing has finished and we can read the
         * buffers. */
        TEST_STATE_IDLE,
        /* The command buffer has begun */
        TEST_STATE_COMMAND_BUFFER,
        /* The render pass has begun */
        TEST_STATE_RENDER_PASS
};

struct test_data {
        struct vr_window *window;
        struct vr_pipeline *pipeline;
        struct vr_list buffers;
        struct test_buffer **ubo_buffers;
        const struct vr_script *script;
        struct test_buffer *vbo_buffer;
        struct test_buffer *index_buffer;
        bool ubo_descriptor_set_bound;
        VkDescriptorSet ubo_descriptor_set;
        VkPipeline bound_pipeline;
        enum test_state test_state;
        bool first_render;
};

static const double
tolerance[4] = { 0.01, 0.01, 0.01, 0.01 };

static struct test_buffer *
allocate_test_buffer(struct test_data *data,
                     size_t size,
                     VkBufferUsageFlagBits usage)
{
        struct test_buffer *buffer = vr_calloc(sizeof *buffer);
        VkResult res;

        vr_list_insert(data->buffers.prev, &buffer->link);

        buffer->size = size;

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
begin_command_buffer(struct test_data *data)
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

        return true;
}

static void
invalidate_ssbos(struct test_data *data)
{
        for (unsigned i = 0; i < data->script->n_buffers; i++) {
                if (data->script->buffers[i].type != VR_SCRIPT_BUFFER_TYPE_SSBO)
                        continue;

                const struct test_buffer *buffer = data->ubo_buffers[i];

                const VkMemoryType *memory_type =
                        (data->window->memory_properties.memoryTypes +
                         buffer->memory_type_index);

                /* We don’t need to do anything if the memory is
                 * already coherent */
                if ((memory_type->propertyFlags &
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                        continue;

                VkMappedMemoryRange memory_range = {
                        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                        .memory = data->ubo_buffers[i]->memory,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                };
                vr_vk.vkInvalidateMappedMemoryRanges(data->window->device,
                                                     1, /* memoryRangeCount */
                                                     &memory_range);
        }
}

static bool
end_command_buffer(struct test_data *data)
{
        VkResult res;
        struct vr_window *window = data->window;

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

        invalidate_ssbos(data);

        return true;
}

static bool
begin_render_pass(struct test_data *data)
{
        VkRenderPassBeginInfo render_pass_begin_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .renderPass = (data->first_render ?
                               data->window->render_pass[0] :
                               data->window->render_pass[1]),
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

        data->bound_pipeline = NULL;
        data->ubo_descriptor_set_bound = false;
        data->first_render = false;

        return true;
}

static bool
end_render_pass(struct test_data *data)
{
        struct vr_window *window = data->window;

        vr_vk.vkCmdEndRenderPass(window->command_buffer);

        VkBufferImageCopy copy_region = {
                .bufferOffset = 0,
                .bufferRowLength = VR_WINDOW_WIDTH,
                .bufferImageHeight = VR_WINDOW_HEIGHT,
                .imageSubresource = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1
                },
                .imageOffset = { 0, 0, 0 },
                .imageExtent = { VR_WINDOW_WIDTH, VR_WINDOW_HEIGHT, 1 }
        };
        vr_vk.vkCmdCopyImageToBuffer(window->command_buffer,
                                     window->color_image,
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     window->linear_buffer,
                                     1, /* regionCount */
                                     &copy_region);

        return true;
}

static bool
set_state(struct test_data *data,
          enum test_state state)
{
        while (data->test_state < state) {
                switch (data->test_state) {
                case TEST_STATE_IDLE:
                        if (!begin_command_buffer(data))
                                return false;
                        break;
                case TEST_STATE_COMMAND_BUFFER:
                        if (!begin_render_pass(data))
                                return false;
                        break;
                case TEST_STATE_RENDER_PASS:
                        vr_fatal("Unexpected test state");
                }
                data->test_state++;
        }

        while (data->test_state > state) {
                switch (data->test_state) {
                case TEST_STATE_IDLE:
                        vr_fatal("Unexpected test state");
                case TEST_STATE_COMMAND_BUFFER:
                        if (!end_command_buffer(data))
                                return false;
                        break;
                case TEST_STATE_RENDER_PASS:
                        if (!end_render_pass(data))
                                return false;
                        break;
                }
                data->test_state--;
        }

        return true;
}

static void
bind_ubo_descriptor_set(struct test_data *data)
{
        if (data->ubo_descriptor_set_bound ||
            data->ubo_descriptor_set == NULL)
                return;

        if (data->pipeline->stages & ~VK_SHADER_STAGE_COMPUTE_BIT) {
                vr_vk.vkCmdBindDescriptorSets(data->window->command_buffer,
                                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              data->pipeline->layout,
                                              0, /* firstSet */
                                              1, /* descriptorSetCount */
                                              &data->ubo_descriptor_set,
                                              0, /* dynamicOffsetCount */
                                              NULL /* pDynamicOffsets */);
        }

        if (data->pipeline->compute_pipeline) {
                vr_vk.vkCmdBindDescriptorSets(data->window->command_buffer,
                                              VK_PIPELINE_BIND_POINT_COMPUTE,
                                              data->pipeline->layout,
                                              0, /* firstSet */
                                              1, /* descriptorSetCount */
                                              &data->ubo_descriptor_set,
                                              0, /* dynamicOffsetCount */
                                              NULL /* pDynamicOffsets */);
        }

        data->ubo_descriptor_set_bound = true;
}

static void
bind_pipeline_for_command(struct test_data *data,
                          const struct vr_script_command *command)
{
        VkPipeline pipeline = vr_pipeline_for_command(data->pipeline, command);

        if (pipeline == data->bound_pipeline)
                return;

        if (pipeline == data->pipeline->compute_pipeline) {
                vr_vk.vkCmdBindPipeline(data->window->command_buffer,
                                        VK_PIPELINE_BIND_POINT_COMPUTE,
                                        pipeline);
        } else {
                vr_vk.vkCmdBindPipeline(data->window->command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline);
        }

        data->bound_pipeline = pipeline;
}

static void
print_command_fail(const struct vr_script_command *command)
{
        printf("Command failed at line %i\n",
               command->line_num);
}

static struct test_buffer *
get_ubo_buffer_for_binding(struct test_data *data,
                           int binding)
{
        for (unsigned i = 0; i < data->script->n_buffers; i++) {
                if (data->script->buffers[i].binding == binding)
                        return data->ubo_buffers[i];
        }

        return NULL;
}

static bool
draw_rect(struct test_data *data,
          const struct vr_script_command *command)
{
        struct test_buffer *buffer;

        buffer = allocate_test_buffer(data,
                                      sizeof (struct vr_pipeline_vertex) * 4,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (buffer == NULL)
                return false;

        if (!set_state(data, TEST_STATE_RENDER_PASS))
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

        v->x = command->draw_rect.x + command->draw_rect.w;
        v->y = command->draw_rect.y + command->draw_rect.h;
        v->z = 0.0f;
        v++;

        vr_flush_memory(data->window,
                        buffer->memory_type_index,
                        buffer->memory,
                        0, /* offset */
                        VK_WHOLE_SIZE);

        bind_ubo_descriptor_set(data);
        bind_pipeline_for_command(data, command);

        vr_vk.vkCmdBindVertexBuffers(data->window->command_buffer,
                                     0, /* firstBinding */
                                     1, /* bindingCount */
                                     &buffer->buffer,
                                     (VkDeviceSize[]) { 0 });
        vr_vk.vkCmdDraw(data->window->command_buffer,
                        4, /* vertexCount */
                        1, /* instanceCount */
                        0, /* firstVertex */
                        0 /* firstInstance */);

        return true;
}

static bool
ensure_index_buffer(struct test_data *data)
{
        if (data->index_buffer)
                return true;

        data->index_buffer =
                allocate_test_buffer(data,
                                     data->script->n_indices *
                                     sizeof data->script->indices[0],
                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        if (data->index_buffer == NULL)
                return false;

        memcpy(data->index_buffer->memory_map,
               data->script->indices,
               data->script->n_indices * sizeof data->script->indices[0]);

        vr_flush_memory(data->window,
                        data->index_buffer->memory_type_index,
                        data->index_buffer->memory,
                        0, /* offset */
                        VK_WHOLE_SIZE);

        return true;
}

static bool
draw_arrays(struct test_data *data,
            const struct vr_script_command *command)
{
        struct vr_vbo *vbo = data->script->vertex_data;

        if (vbo == NULL) {
                print_command_fail(command);
                vr_error_message("draw arrays command used with no vertex "
                                 "data section");
                return false;
        }

        if (data->vbo_buffer == NULL) {
                data->vbo_buffer =
                        allocate_test_buffer(data,
                                             vbo->stride *
                                             vbo->num_rows,
                                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                if (data->vbo_buffer == NULL)
                        return false;

                memcpy(data->vbo_buffer->memory_map,
                       vbo->raw_data,
                       vbo->stride * vbo->num_rows);

                vr_flush_memory(data->window,
                                data->vbo_buffer->memory_type_index,
                                data->vbo_buffer->memory,
                                0, /* offset */
                                VK_WHOLE_SIZE);
        }

        if (!set_state(data, TEST_STATE_RENDER_PASS))
                return false;

        bind_ubo_descriptor_set(data);
        bind_pipeline_for_command(data, command);

        vr_vk.vkCmdBindVertexBuffers(data->window->command_buffer,
                                     0, /* firstBinding */
                                     1, /* bindingCount */
                                     &data->vbo_buffer->buffer,
                                     (VkDeviceSize[]) { 0 });

        if (command->draw_arrays.indexed) {
                if (!ensure_index_buffer(data))
                        return false;
                vr_vk.vkCmdBindIndexBuffer(data->window->command_buffer,
                                           data->index_buffer->buffer,
                                           0, /* offset */
                                           VK_INDEX_TYPE_UINT16);
                vr_vk.vkCmdDrawIndexed(data->window->command_buffer,
                                       command->draw_arrays.vertex_count,
                                       command->draw_arrays.instance_count,
                                       0, /* firstIndex */
                                       command->draw_arrays.first_vertex,
                                       command->draw_arrays.first_instance);
        } else {
                vr_vk.vkCmdDraw(data->window->command_buffer,
                                command->draw_arrays.vertex_count,
                                command->draw_arrays.instance_count,
                                command->draw_arrays.first_vertex,
                                command->draw_arrays.first_instance);
        }

        return true;
}

static bool
dispatch_compute(struct test_data *data,
                 const struct vr_script_command *command)
{
        if (!set_state(data, TEST_STATE_COMMAND_BUFFER))
                return false;

        bind_ubo_descriptor_set(data);
        bind_pipeline_for_command(data, command);

        vr_vk.vkCmdDispatch(data->window->command_buffer,
                            command->dispatch_compute.x,
                            command->dispatch_compute.y,
                            command->dispatch_compute.z);

        return true;
}

static bool
compare_pixels(const double *color1,
               const double *color2,
               const double *tolerance,
               int n_components)
{
        for (int p = 0; p < n_components; ++p)
                if (fabsf(color1[p] - color2[p]) > tolerance[p])
                        return false;
        return true;
}

static void
print_components_double(const double *pixel,
                        int n_components)
{
        int p;
        for (p = 0; p < n_components; ++p)
                printf(" %f", pixel[p]);
}

static void
print_bad_pixel(int x, int y,
                int n_components,
                const double *expected,
                const double *observed)
{
        printf("Probe color at (%i,%i)\n"
               "  Expected:",
               x, y);
        print_components_double(expected, n_components);
        printf("\n"
               "  Observed:");
        print_components_double(observed, n_components);
        printf("\n");
}

static bool
probe_rect(struct test_data *data,
           const struct vr_script_command *command)
{
        int n_components = command->probe_rect.n_components;
        const struct vr_format *format = data->window->framebuffer_format;
        int format_size = vr_format_get_size(format);

        /* End the paint to copy the framebuffer into the linear buffer */
        if (!set_state(data, TEST_STATE_IDLE))
                return false;

        for (int y = 0; y < command->probe_rect.h; y++) {
                const uint8_t *p =
                        ((y + command->probe_rect.y) *
                         data->window->linear_memory_stride +
                         command->probe_rect.x * format_size +
                         (uint8_t *) data->window->linear_memory_map);
                for (int x = 0; x < command->probe_rect.w; x++) {
                        double pixel[4];
                        vr_format_load_pixel(format, p, pixel);
                        p += format_size;

                        if (!compare_pixels(pixel,
                                            command->probe_rect.color,
                                            tolerance,
                                            n_components)) {
                                print_command_fail(command);
                                print_bad_pixel(x + command->probe_rect.x,
                                                y + command->probe_rect.y,
                                                n_components,
                                                command->probe_rect.color,
                                                pixel);
                                return false;
                        }
                }
        }

        return true;
}

static bool
probe_ssbo(struct test_data *data,
           const struct vr_script_command *command)
{
        if (!set_state(data, TEST_STATE_IDLE))
                return false;

        struct test_buffer *buffer =
                get_ubo_buffer_for_binding(data,
                                           command->probe_ssbo.binding);

        if (buffer == NULL) {
                print_command_fail(command);
                vr_error_message("Invalid binding in probe command");
                return false;
        }

        const struct vr_script_value *expected = &command->probe_ssbo.value;
        size_t type_size = vr_script_type_size(expected->type);

        if (command->probe_ssbo.offset + type_size > buffer->size) {
                print_command_fail(command);
                vr_error_message("Invalid offset in probe command");
                return false;
        }

        struct vr_script_value observed;
        observed.type = expected->type;
        memcpy(&observed.i,
               (const uint8_t *) buffer->memory_map +
               command->probe_ssbo.offset,
               type_size);

        if (!vr_script_compare_values(command->probe_ssbo.comparison,
                                      &observed,
                                      expected)) {
                print_command_fail(command);
                vr_error_message("SSBO probe failed");
                if (observed.type == VR_SCRIPT_TYPE_UINT) {
                        vr_error_message("  Reference: %u\n"
                                         "  Observed:  %u",
                                         expected->u,
                                         observed.u);
                }
                return false;
        }

        return true;
}

static bool
set_push_constant(struct test_data *data,
                  const struct vr_script_command *command)
{
        if (data->test_state < TEST_STATE_COMMAND_BUFFER &&
            !set_state(data, TEST_STATE_COMMAND_BUFFER))
                return false;

        vr_vk.vkCmdPushConstants(data->window->command_buffer,
                                 data->pipeline->layout,
                                 data->pipeline->stages,
                                 command->set_push_constant.offset,
                                 command->set_push_constant.size,
                                 command->set_push_constant.data);

        return true;
}

static bool
allocate_ubo_buffers(struct test_data *data)
{
        VkResult res;
        VkDescriptorSetAllocateInfo allocate_info = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = data->window->descriptor_pool,
                .descriptorSetCount = 1,
                .pSetLayouts = &data->pipeline->descriptor_set_layout
        };
        res = vr_vk.vkAllocateDescriptorSets(data->window->device,
                                             &allocate_info,
                                             &data->ubo_descriptor_set);
        if (res != VK_SUCCESS) {
                data->ubo_descriptor_set = NULL;
                vr_error_message("Error allocationg descriptor set");
                return false;
        }

        data->ubo_buffers = vr_alloc(sizeof *data->ubo_buffers *
                                     data->script->n_buffers);

        for (unsigned i = 0; i < data->script->n_buffers; i++) {
                const struct vr_script_buffer *script_buffer =
                        data->script->buffers + i;

                enum VkBufferUsageFlagBits usage;
                VkDescriptorType descriptor_type;
                switch (script_buffer->type) {
                case VR_SCRIPT_BUFFER_TYPE_UBO:
                        usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                        descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        goto found_type;
                case VR_SCRIPT_BUFFER_TYPE_SSBO:
                        usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                        descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        goto found_type;
                }
                vr_fatal("Unexpected buffer type");
        found_type:
                ((void) 0);

                struct test_buffer *test_buffer =
                        allocate_test_buffer(data, script_buffer->size, usage);

                if (test_buffer == NULL)
                        return false;

                data->ubo_buffers[i] = test_buffer;

                VkDescriptorBufferInfo buffer_info = {
                        .buffer = test_buffer->buffer,
                        .offset = 0,
                        .range = VK_WHOLE_SIZE
                };
                VkWriteDescriptorSet write = {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = data->ubo_descriptor_set,
                        .dstBinding = script_buffer->binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = descriptor_type,
                        .pBufferInfo = &buffer_info
                };
                vr_vk.vkUpdateDescriptorSets(data->window->device,
                                             1, /* descriptorWriteCount */
                                             &write,
                                             0, /* descriptorCopyCount */
                                             NULL /* pDescriptorCopies */);
        }

        return true;
}

static bool
set_buffer_subdata(struct test_data *data,
                   const struct vr_script_command *command)
{
        struct test_buffer *buffer =
                get_ubo_buffer_for_binding(data,
                                           command->set_buffer_subdata.binding);
        assert(buffer);

        memcpy((uint8_t *) buffer->memory_map +
               command->set_buffer_subdata.offset,
               command->set_buffer_subdata.data,
               command->set_buffer_subdata.size);
        vr_flush_memory(data->window,
                        buffer->memory_type_index,
                        buffer->memory,
                        command->set_push_constant.offset,
                        command->set_buffer_subdata.size);

        return true;
}

static bool
clear(struct test_data *data,
      const struct vr_script_command *command)
{
        if (!set_state(data, TEST_STATE_RENDER_PASS))
                return false;

        VkImageAspectFlags depth_stencil_flags = 0;
        const struct vr_format *depth_stencil_format =
                data->window->depth_stencil_format;

        if (data->window->depth_stencil_format) {
                for (int i = 0; i < depth_stencil_format->n_parts; i++) {
                        switch (depth_stencil_format->parts[i].component) {
                        case VR_FORMAT_COMPONENT_D:
                                depth_stencil_flags |=
                                        VK_IMAGE_ASPECT_DEPTH_BIT;
                                break;
                        case VR_FORMAT_COMPONENT_S:
                                depth_stencil_flags |=
                                        VK_IMAGE_ASPECT_STENCIL_BIT;
                                break;
                        default:
                                break;
                        }
                }
        }

        VkClearAttachment clear_attachments[] = {
                {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .colorAttachment = 0,
                },
                {
                        .aspectMask = depth_stencil_flags,
                        .clearValue = { .depthStencil =
                                        { .depth = command->clear.depth,
                                          .stencil = command->clear.stencil } }
                },
        };
        VkClearRect clear_rect = {
                .rect = {
                        .offset = { 0, 0 },
                        .extent = { VR_WINDOW_WIDTH, VR_WINDOW_HEIGHT }
                },
                .baseArrayLayer = 0,
                .layerCount = 1
        };
        memcpy(clear_attachments[0].clearValue.color.float32,
               command->clear.color,
               sizeof command->clear.color);

        int n_attachments;

        if (depth_stencil_flags)
                n_attachments = 2;
        else
                n_attachments = 1;

        vr_vk.vkCmdClearAttachments(data->window->command_buffer,
                                    n_attachments,
                                    clear_attachments,
                                    1,
                                    &clear_rect);
        return true;
}

static bool
run_commands(struct test_data *data)
{
        const struct vr_script *script = data->script;
        bool ret = true;

        for (int i = 0; i < script->n_commands; i++) {
                const struct vr_script_command *command = script->commands + i;

                switch (command->op) {
                case VR_SCRIPT_OP_DRAW_RECT:
                        if (!draw_rect(data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_DRAW_ARRAYS:
                        if (!draw_arrays(data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_DISPATCH_COMPUTE:
                        if (!dispatch_compute(data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_PROBE_RECT:
                        if (!probe_rect(data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_PROBE_SSBO:
                        if (!probe_ssbo(data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_SET_PUSH_CONSTANT:
                        if (!set_push_constant(data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_SET_BUFFER_SUBDATA:
                        if (!set_buffer_subdata(data, command))
                                ret = false;
                        break;
                case VR_SCRIPT_OP_CLEAR:
                        if (!clear(data, command))
                                ret = false;
                        break;
                }
        }

        return ret;
}

bool
vr_test_run(struct vr_window *window,
            struct vr_pipeline *pipeline,
            const struct vr_script *script)
{
        struct test_data data = {
                .window = window,
                .pipeline = pipeline,
                .script = script,
                .test_state = TEST_STATE_IDLE,
                .first_render = true,
        };
        bool ret = true;

        vr_list_init(&data.buffers);

        if (script->n_buffers > 0 && !allocate_ubo_buffers(&data)) {
                ret = false;
        } else {
                if (!run_commands(&data))
                        ret = false;

                if (!set_state(&data, TEST_STATE_IDLE))
                        ret = false;
        }

        struct test_buffer *buffer, *tmp;
        vr_list_for_each_safe(buffer, tmp, &data.buffers, link) {
                free_test_buffer(&data, buffer);
        }

        vr_free(data.ubo_buffers);

        if (data.ubo_descriptor_set) {
                vr_vk.vkFreeDescriptorSets(window->device,
                                           window->descriptor_pool,
                                           1, /* descriptorSetCount */
                                           &data.ubo_descriptor_set);
        }

        return ret;
}
