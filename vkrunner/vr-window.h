/*
 * vkrunner
 *
 * Copyright (C) 2017 Neil Roberts
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

#ifndef VR_WINDOW_H
#define VR_WINDOW_H

#include <stdbool.h>
#include "vr-vk.h"
#include "vr-format.h"
#include "vr-result.h"
#include "vr-config.h"
#include "vr-context.h"
#include "vr-window-format.h"

struct vr_window;

struct vr_context *
vr_window_get_context(struct vr_window *window);

const struct vr_window_format *
vr_window_get_format(const struct vr_window *window);

const struct vr_vk_device *
vr_window_get_vkdev(const struct vr_window *window);

VkDevice
vr_window_get_device(const struct vr_window *window);

VkRenderPass
vr_window_get_render_pass(const struct vr_window *window,
                          bool first_render);

const struct vr_config *
vr_window_get_config(const struct vr_window *window);

bool
vr_window_need_linear_memory_invalidate(const struct vr_window *window);

VkDeviceMemory
vr_window_get_linear_memory(const struct vr_window *window);

VkDeviceSize
vr_window_get_linear_memory_stride(const struct vr_window *window);

VkBuffer
vr_window_get_linear_buffer(const struct vr_window *window);

const void *
vr_window_get_linear_memory_map(struct vr_window *window);

VkFramebuffer
vr_window_get_framebuffer(const struct vr_window *window);

VkImage
vr_window_get_color_image(const struct vr_window *window);

#endif /* VR_WINDOW_H */
