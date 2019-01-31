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

#ifndef VR_FEATURE_H
#define VR_FEATURE_H

#include <stdlib.h>
#include "vr-vk.h"

struct vr_feature_extension {
        /* The name of the extension which provides this set of
         * features, or NULL if it is in VkPhysicalDeviceFeatures
         */
        const char *name;
        /* The size of the corresponding features struct */
        size_t struct_size;
        VkStructureType struct_type;
        /* List of features in this extension, terminated by a feature
         * with a NULL name.
         */
        const struct vr_feature_offset *offsets;
};

struct vr_feature_offset {
        const char *name;
        size_t offset;
};

/* Array of feature extensions, terminated by one with a 0 struct size. */
extern const struct vr_feature_extension
vr_feature_extensions[];

/* The features provided by VkPhysicalDeviceFeatures */
extern const struct vr_feature_offset
vr_feature_base_offsets[];

#endif /* VR_FEATURE_H */
