/*
 * vkrunner
 *
 * Copyright (C) 2019 Intel Corporation
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

#ifndef VR_REQUIREMENTS_H
#define VR_REQUIREMENTS_H

#include <stdlib.h>
#include <stdbool.h>

#include "vr-vk.h"

struct vr_requirements;

struct vr_requirements *
vr_requirements_new(void);

const char* const*
vr_requirements_get_extensions(const struct vr_requirements *reqs);

size_t
vr_requirements_get_n_extensions(const struct vr_requirements *reqs);

const void *
vr_requirements_get_structures(const struct vr_requirements *reqs);

const VkPhysicalDeviceFeatures *
vr_requirements_get_base_features(const struct vr_requirements *reqs);

void
vr_requirements_add(struct vr_requirements *reqs,
                    const char *name);

bool
vr_requirements_equal(const struct vr_requirements *reqs_a,
                      const struct vr_requirements *reqs_b);

struct vr_requirements *
vr_requirements_copy(const struct vr_requirements *reqs);

bool
vr_requirements_check(const struct vr_requirements *reqs,
                      struct vr_vk *vkfn,
                      VkInstance instance,
                      VkPhysicalDevice device);

void
vr_requirements_free(struct vr_requirements *reqs);

#endif /* VR_REQUIREMENTS_H */
