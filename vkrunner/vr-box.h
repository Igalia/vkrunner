/*
 * vkrunner
 *
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

#ifndef VR_BOX_H
#define VR_BOX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "vr-tolerance.h"

enum vr_box_layout_std {
        VR_BOX_LAYOUT_STD_140,
        VR_BOX_LAYOUT_STD_430
};

enum vr_box_major_axis {
        VR_BOX_MAJOR_AXIS_COLUMN,
        VR_BOX_MAJOR_AXIS_ROW
};

struct vr_box_layout {
        enum vr_box_layout_std std;
        enum vr_box_major_axis major;
};

enum vr_box_type {
        VR_BOX_TYPE_INT,
        VR_BOX_TYPE_UINT,
        VR_BOX_TYPE_INT8,
        VR_BOX_TYPE_UINT8,
        VR_BOX_TYPE_INT16,
        VR_BOX_TYPE_UINT16,
        VR_BOX_TYPE_INT64,
        VR_BOX_TYPE_UINT64,
        VR_BOX_TYPE_FLOAT,
        VR_BOX_TYPE_DOUBLE,
        VR_BOX_TYPE_VEC2,
        VR_BOX_TYPE_VEC3,
        VR_BOX_TYPE_VEC4,
        VR_BOX_TYPE_DVEC2,
        VR_BOX_TYPE_DVEC3,
        VR_BOX_TYPE_DVEC4,
        VR_BOX_TYPE_IVEC2,
        VR_BOX_TYPE_IVEC3,
        VR_BOX_TYPE_IVEC4,
        VR_BOX_TYPE_UVEC2,
        VR_BOX_TYPE_UVEC3,
        VR_BOX_TYPE_UVEC4,
        VR_BOX_TYPE_I8VEC2,
        VR_BOX_TYPE_I8VEC3,
        VR_BOX_TYPE_I8VEC4,
        VR_BOX_TYPE_U8VEC2,
        VR_BOX_TYPE_U8VEC3,
        VR_BOX_TYPE_U8VEC4,
        VR_BOX_TYPE_I16VEC2,
        VR_BOX_TYPE_I16VEC3,
        VR_BOX_TYPE_I16VEC4,
        VR_BOX_TYPE_U16VEC2,
        VR_BOX_TYPE_U16VEC3,
        VR_BOX_TYPE_U16VEC4,
        VR_BOX_TYPE_I64VEC2,
        VR_BOX_TYPE_I64VEC3,
        VR_BOX_TYPE_I64VEC4,
        VR_BOX_TYPE_U64VEC2,
        VR_BOX_TYPE_U64VEC3,
        VR_BOX_TYPE_U64VEC4,
        VR_BOX_TYPE_MAT2,
        VR_BOX_TYPE_MAT2X3,
        VR_BOX_TYPE_MAT2X4,
        VR_BOX_TYPE_MAT3X2,
        VR_BOX_TYPE_MAT3,
        VR_BOX_TYPE_MAT3X4,
        VR_BOX_TYPE_MAT4X2,
        VR_BOX_TYPE_MAT4X3,
        VR_BOX_TYPE_MAT4,
        VR_BOX_TYPE_DMAT2,
        VR_BOX_TYPE_DMAT2X3,
        VR_BOX_TYPE_DMAT2X4,
        VR_BOX_TYPE_DMAT3X2,
        VR_BOX_TYPE_DMAT3,
        VR_BOX_TYPE_DMAT3X4,
        VR_BOX_TYPE_DMAT4X2,
        VR_BOX_TYPE_DMAT4X3,
        VR_BOX_TYPE_DMAT4,
};

enum vr_box_base_type {
        VR_BOX_BASE_TYPE_INT,
        VR_BOX_BASE_TYPE_UINT,
        VR_BOX_BASE_TYPE_INT8,
        VR_BOX_BASE_TYPE_UINT8,
        VR_BOX_BASE_TYPE_INT16,
        VR_BOX_BASE_TYPE_UINT16,
        VR_BOX_BASE_TYPE_INT64,
        VR_BOX_BASE_TYPE_UINT64,
        VR_BOX_BASE_TYPE_FLOAT,
        VR_BOX_BASE_TYPE_DOUBLE
};

enum vr_box_comparison {
        VR_BOX_COMPARISON_EQUAL,
        VR_BOX_COMPARISON_FUZZY_EQUAL,
        VR_BOX_COMPARISON_NOT_EQUAL,
        VR_BOX_COMPARISON_LESS,
        VR_BOX_COMPARISON_GREATER_EQUAL,
        VR_BOX_COMPARISON_GREATER,
        VR_BOX_COMPARISON_LESS_EQUAL
};

struct vr_box_type_info {
        enum vr_box_base_type base_type;
        int columns;
        int rows;
};

typedef bool
(* vr_box_for_each_component_cb_t)(enum vr_box_base_type type,
                                   size_t offset,
                                   void *user_data);

void
vr_box_for_each_component(enum vr_box_type type,
                          const struct vr_box_layout *layout,
                          vr_box_for_each_component_cb_t cb,
                          void *user_data);

bool
vr_box_compare(enum vr_box_comparison comparison,
               const struct vr_tolerance *tolerance,
               enum vr_box_type type,
               const struct vr_box_layout *layout,
               const void *a,
               const void *b);

size_t
vr_box_base_type_size(enum vr_box_base_type type);

size_t
vr_box_type_base_alignment(enum vr_box_type type,
                           const struct vr_box_layout *layout);

size_t
vr_box_type_matrix_stride(enum vr_box_type type,
                          const struct vr_box_layout *layout);

size_t
vr_box_type_array_stride(enum vr_box_type type,
                         const struct vr_box_layout *layout);

size_t
vr_box_type_size(enum vr_box_type type,
                 const struct vr_box_layout *layout);

const struct vr_box_type_info *
vr_box_type_get_info(enum vr_box_type type);

#endif /* VR_BOX_H */
