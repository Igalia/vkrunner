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

#include "config.h"

#include "vr-box.h"

#include <assert.h>

#include "vr-util.h"
#include "vr-tolerance.h"

static const struct vr_box_type_info
type_infos[] = {
        [VR_BOX_TYPE_INT] = { VR_BOX_BASE_TYPE_INT, 1, 1 },
        [VR_BOX_TYPE_UINT] = { VR_BOX_BASE_TYPE_UINT, 1, 1 },
        [VR_BOX_TYPE_INT8] = { VR_BOX_BASE_TYPE_INT8, 1, 1 },
        [VR_BOX_TYPE_UINT8] = { VR_BOX_BASE_TYPE_UINT8, 1, 1 },
        [VR_BOX_TYPE_INT16] = { VR_BOX_BASE_TYPE_INT16, 1, 1 },
        [VR_BOX_TYPE_UINT16] = { VR_BOX_BASE_TYPE_UINT16, 1, 1 },
        [VR_BOX_TYPE_INT64] = { VR_BOX_BASE_TYPE_INT64, 1, 1 },
        [VR_BOX_TYPE_UINT64] = { VR_BOX_BASE_TYPE_UINT64, 1, 1 },
        [VR_BOX_TYPE_FLOAT] = { VR_BOX_BASE_TYPE_FLOAT, 1, 1 },
        [VR_BOX_TYPE_DOUBLE] = { VR_BOX_BASE_TYPE_DOUBLE, 1, 1 },
        [VR_BOX_TYPE_VEC2] = { VR_BOX_BASE_TYPE_FLOAT, 1, 2 },
        [VR_BOX_TYPE_VEC3] = { VR_BOX_BASE_TYPE_FLOAT, 1, 3 },
        [VR_BOX_TYPE_VEC4] = { VR_BOX_BASE_TYPE_FLOAT, 1, 4 },
        [VR_BOX_TYPE_DVEC2] = { VR_BOX_BASE_TYPE_DOUBLE, 1, 2 },
        [VR_BOX_TYPE_DVEC3] = { VR_BOX_BASE_TYPE_DOUBLE, 1, 3 },
        [VR_BOX_TYPE_DVEC4] = { VR_BOX_BASE_TYPE_DOUBLE, 1, 4 },
        [VR_BOX_TYPE_IVEC2] = { VR_BOX_BASE_TYPE_INT, 1, 2 },
        [VR_BOX_TYPE_IVEC3] = { VR_BOX_BASE_TYPE_INT, 1, 3 },
        [VR_BOX_TYPE_IVEC4] = { VR_BOX_BASE_TYPE_INT, 1, 4 },
        [VR_BOX_TYPE_UVEC2] = { VR_BOX_BASE_TYPE_UINT, 1, 2 },
        [VR_BOX_TYPE_UVEC3] = { VR_BOX_BASE_TYPE_UINT, 1, 3 },
        [VR_BOX_TYPE_UVEC4] = { VR_BOX_BASE_TYPE_UINT, 1, 4 },
        [VR_BOX_TYPE_I8VEC2] = { VR_BOX_BASE_TYPE_INT8, 1, 2 },
        [VR_BOX_TYPE_I8VEC3] = { VR_BOX_BASE_TYPE_INT8, 1, 3 },
        [VR_BOX_TYPE_I8VEC4] = { VR_BOX_BASE_TYPE_INT8, 1, 4 },
        [VR_BOX_TYPE_U8VEC2] = { VR_BOX_BASE_TYPE_UINT8, 1, 2 },
        [VR_BOX_TYPE_U8VEC3] = { VR_BOX_BASE_TYPE_UINT8, 1, 3 },
        [VR_BOX_TYPE_U8VEC4] = { VR_BOX_BASE_TYPE_UINT8, 1, 4 },
        [VR_BOX_TYPE_I16VEC2] = { VR_BOX_BASE_TYPE_INT16, 1, 2 },
        [VR_BOX_TYPE_I16VEC3] = { VR_BOX_BASE_TYPE_INT16, 1, 3 },
        [VR_BOX_TYPE_I16VEC4] = { VR_BOX_BASE_TYPE_INT16, 1, 4 },
        [VR_BOX_TYPE_U16VEC2] = { VR_BOX_BASE_TYPE_UINT16, 1, 2 },
        [VR_BOX_TYPE_U16VEC3] = { VR_BOX_BASE_TYPE_UINT16, 1, 3 },
        [VR_BOX_TYPE_U16VEC4] = { VR_BOX_BASE_TYPE_UINT16, 1, 4 },
        [VR_BOX_TYPE_I64VEC2] = { VR_BOX_BASE_TYPE_INT64, 1, 2 },
        [VR_BOX_TYPE_I64VEC3] = { VR_BOX_BASE_TYPE_INT64, 1, 3 },
        [VR_BOX_TYPE_I64VEC4] = { VR_BOX_BASE_TYPE_INT64, 1, 4 },
        [VR_BOX_TYPE_U64VEC2] = { VR_BOX_BASE_TYPE_UINT64, 1, 2 },
        [VR_BOX_TYPE_U64VEC3] = { VR_BOX_BASE_TYPE_UINT64, 1, 3 },
        [VR_BOX_TYPE_U64VEC4] = { VR_BOX_BASE_TYPE_UINT64, 1, 4 },
        [VR_BOX_TYPE_MAT2] = { VR_BOX_BASE_TYPE_FLOAT, 2, 2 },
        [VR_BOX_TYPE_MAT2X3] = { VR_BOX_BASE_TYPE_FLOAT, 2, 3 },
        [VR_BOX_TYPE_MAT2X4] = { VR_BOX_BASE_TYPE_FLOAT, 2, 4 },
        [VR_BOX_TYPE_MAT3X2] = { VR_BOX_BASE_TYPE_FLOAT, 3, 2 },
        [VR_BOX_TYPE_MAT3] = { VR_BOX_BASE_TYPE_FLOAT, 3, 3 },
        [VR_BOX_TYPE_MAT3X4] = { VR_BOX_BASE_TYPE_FLOAT, 3, 4 },
        [VR_BOX_TYPE_MAT4X2] = { VR_BOX_BASE_TYPE_FLOAT, 4, 2 },
        [VR_BOX_TYPE_MAT4X3] = { VR_BOX_BASE_TYPE_FLOAT, 4, 3 },
        [VR_BOX_TYPE_MAT4] = { VR_BOX_BASE_TYPE_FLOAT, 4, 4 },
        [VR_BOX_TYPE_DMAT2] = { VR_BOX_BASE_TYPE_DOUBLE, 2, 2 },
        [VR_BOX_TYPE_DMAT2X3] = { VR_BOX_BASE_TYPE_DOUBLE, 2, 3 },
        [VR_BOX_TYPE_DMAT2X4] = { VR_BOX_BASE_TYPE_DOUBLE, 2, 4 },
        [VR_BOX_TYPE_DMAT3X2] = { VR_BOX_BASE_TYPE_DOUBLE, 3, 2 },
        [VR_BOX_TYPE_DMAT3] = { VR_BOX_BASE_TYPE_DOUBLE, 3, 3 },
        [VR_BOX_TYPE_DMAT3X4] = { VR_BOX_BASE_TYPE_DOUBLE, 3, 4 },
        [VR_BOX_TYPE_DMAT4X2] = { VR_BOX_BASE_TYPE_DOUBLE, 4, 2 },
        [VR_BOX_TYPE_DMAT4X3] = { VR_BOX_BASE_TYPE_DOUBLE, 4, 3 },
        [VR_BOX_TYPE_DMAT4] = { VR_BOX_BASE_TYPE_DOUBLE, 4, 4 },
};

size_t
vr_box_base_type_size(enum vr_box_base_type type)
{
        switch (type) {
        case VR_BOX_BASE_TYPE_INT: return sizeof (int32_t);
        case VR_BOX_BASE_TYPE_UINT: return sizeof (uint32_t);
        case VR_BOX_BASE_TYPE_INT8: return sizeof (int8_t);
        case VR_BOX_BASE_TYPE_UINT8: return sizeof (uint8_t);
        case VR_BOX_BASE_TYPE_INT16: return sizeof (int16_t);
        case VR_BOX_BASE_TYPE_UINT16: return sizeof (uint16_t);
        case VR_BOX_BASE_TYPE_INT64: return sizeof (int64_t);
        case VR_BOX_BASE_TYPE_UINT64: return sizeof (uint64_t);
        case VR_BOX_BASE_TYPE_FLOAT: return sizeof (float);
        case VR_BOX_BASE_TYPE_DOUBLE: return sizeof (double);
        }

        vr_fatal("Unknown base type");
}

static void
get_major_minor(enum vr_box_type type,
                const struct vr_box_layout *layout,
                int *major,
                int *minor)
{
        const struct vr_box_type_info *info = type_infos + type;

        switch (layout->major) {
        case VR_BOX_MAJOR_AXIS_COLUMN:
                *major = info->columns;
                *minor = info->rows;
                return;
        case VR_BOX_MAJOR_AXIS_ROW:
                *major = info->rows;
                *minor = info->columns;
                return;
        }

        vr_fatal("Unexpected matrix major axis");
}

size_t
vr_box_type_base_alignment(enum vr_box_type type,
                           const struct vr_box_layout *layout)
{
        const struct vr_box_type_info *info = type_infos + type;
        int component_size = vr_box_base_type_size(info->base_type);
        int major, minor;

        get_major_minor(type, layout, &major, &minor);

        if (minor == 3)
                return component_size * 4;
        else
                return component_size * minor;
}

size_t
vr_box_type_matrix_stride(enum vr_box_type type,
                          const struct vr_box_layout *layout)
{
        size_t base_alignment = vr_box_type_base_alignment(type, layout);

        switch (layout->std) {
        case VR_BOX_LAYOUT_STD_140:
                /* according to std140 the size is rounded up to a vec4 */
                return vr_align(base_alignment, 16);
        case VR_BOX_LAYOUT_STD_430:
                return base_alignment;
        }

        vr_fatal("Unknown layout std");
}

size_t
vr_box_type_array_stride(enum vr_box_type type,
                         const struct vr_box_layout *layout)
{
        size_t matrix_stride = vr_box_type_matrix_stride(type, layout);
        int major, minor;

        get_major_minor(type, layout, &major, &minor);

        return matrix_stride * major;
}

size_t
vr_box_type_size(enum vr_box_type type,
                 const struct vr_box_layout *layout)
{
        const struct vr_box_type_info *info = type_infos + type;
        size_t matrix_stride = vr_box_type_matrix_stride(type, layout);
        size_t base_size = vr_box_base_type_size(info->base_type);
        int major, minor;

        get_major_minor(type, layout, &major, &minor);

        return (major - 1) * matrix_stride + base_size * minor;
}

static void
get_axis_offsets(enum vr_box_type type,
                 const struct vr_box_layout *layout,
                 int *column_offset,
                 int *row_offset)
{
        const struct vr_box_type_info *info = type_infos + type;
        size_t stride = vr_box_type_matrix_stride(type, layout);
        size_t base_size = vr_box_base_type_size(info->base_type);

        switch (layout->major) {
        case VR_BOX_MAJOR_AXIS_COLUMN:
                *column_offset = stride - base_size * info->rows;
                *row_offset = base_size;
                return;
        case VR_BOX_MAJOR_AXIS_ROW:
                *column_offset = base_size - stride * info->rows;
                *row_offset = stride;
                return;
        }

        vr_fatal("Unexpected matrix major axis");
}

void
vr_box_for_each_component(enum vr_box_type type,
                          const struct vr_box_layout *layout,
                          vr_box_for_each_component_cb_t cb,
                          void *user_data)
{
        const struct vr_box_type_info *info = type_infos + type;
        long offset = 0;
        int column_offset, row_offset;

        get_axis_offsets(type, layout, &column_offset, &row_offset);

        for (int col = 0; col < info->columns; col++) {
                for (int row = 0; row < info->rows; row++) {
                        if (!cb(info->base_type,
                                offset,
                                user_data)) {
                                return;
                        }
                        offset += row_offset;
                }
                offset += column_offset;
        }
}

struct compare_closure {
        enum vr_box_comparison comparison;
        const uint8_t *a;
        const uint8_t *b;
        const struct vr_tolerance *tolerance;
        int index;
        int index_max;
        bool result;
};

static bool
compare_signed(enum vr_box_comparison comparison,
               int64_t a,
               int64_t b)
{
        switch (comparison) {
        case VR_BOX_COMPARISON_EQUAL:
        case VR_BOX_COMPARISON_FUZZY_EQUAL:
                return a == b;
        case VR_BOX_COMPARISON_NOT_EQUAL:
                return a != b;
        case VR_BOX_COMPARISON_LESS:
                return a < b;
        case VR_BOX_COMPARISON_GREATER_EQUAL:
                return a >= b;
        case VR_BOX_COMPARISON_GREATER:
                return a > b;
        case VR_BOX_COMPARISON_LESS_EQUAL:
                return a <= b;
        }

        vr_fatal("Unexpected comparison");
}

static bool
compare_unsigned(enum vr_box_comparison comparison,
                 uint64_t a,
                 uint64_t b)
{
        switch (comparison) {
        case VR_BOX_COMPARISON_EQUAL:
        case VR_BOX_COMPARISON_FUZZY_EQUAL:
                return a == b;
        case VR_BOX_COMPARISON_NOT_EQUAL:
                return a != b;
        case VR_BOX_COMPARISON_LESS:
                return a < b;
        case VR_BOX_COMPARISON_GREATER_EQUAL:
                return a >= b;
        case VR_BOX_COMPARISON_GREATER:
                return a > b;
        case VR_BOX_COMPARISON_LESS_EQUAL:
                return a <= b;
        }

        vr_fatal("Unexpected comparison");
}

static bool
compare_double(const struct compare_closure *data,
               double a,
               double b)
{
        switch (data->comparison) {
        case VR_BOX_COMPARISON_EQUAL:
                return a == b;
        case VR_BOX_COMPARISON_FUZZY_EQUAL:
                return vr_tolerance_equal(data->tolerance,
                                          data->index % data->index_max,
                                          a,
                                          b);
        case VR_BOX_COMPARISON_NOT_EQUAL:
                return a != b;
        case VR_BOX_COMPARISON_LESS:
                return a < b;
        case VR_BOX_COMPARISON_GREATER_EQUAL:
                return a >= b;
        case VR_BOX_COMPARISON_GREATER:
                return a > b;
        case VR_BOX_COMPARISON_LESS_EQUAL:
                return a <= b;
        }

        vr_fatal("Unexpected comparison");
}

static bool
compare_value(const struct compare_closure *data,
              enum vr_box_base_type type,
              const void *a,
              const void *b)
{
        switch (type) {
        case VR_BOX_BASE_TYPE_INT:
                return compare_signed(data->comparison,
                                      *(const int32_t *) a,
                                      *(const int32_t *) b);
        case VR_BOX_BASE_TYPE_UINT:
                return compare_unsigned(data->comparison,
                                        *(const uint32_t *) a,
                                        *(const uint32_t *) b);
        case VR_BOX_BASE_TYPE_INT8:
                return compare_signed(data->comparison,
                                      *(const int8_t *) a,
                                      *(const int8_t *) b);
        case VR_BOX_BASE_TYPE_UINT8:
                return compare_unsigned(data->comparison,
                                        *(const uint8_t *) a,
                                        *(const uint8_t *) b);
        case VR_BOX_BASE_TYPE_INT16:
                return compare_signed(data->comparison,
                                      *(const int16_t *) a,
                                      *(const int16_t *) b);
        case VR_BOX_BASE_TYPE_UINT16:
                return compare_unsigned(data->comparison,
                                        *(const uint16_t *) a,
                                        *(const uint16_t *) b);
        case VR_BOX_BASE_TYPE_INT64:
                return compare_signed(data->comparison,
                                      *(const int64_t *) a,
                                      *(const int64_t *) b);
        case VR_BOX_BASE_TYPE_UINT64:
                return compare_unsigned(data->comparison,
                                        *(const uint64_t *) a,
                                        *(const uint64_t *) b);
        case VR_BOX_BASE_TYPE_FLOAT:
                return compare_double(data,
                                      *(const float *) a,
                                      *(const float *) b);
        case VR_BOX_BASE_TYPE_DOUBLE:
                return compare_double(data,
                                      *(const double *) a,
                                      *(const double *) b);
        }

        vr_fatal("Unexpected base type");
}

static bool
compare_cb(enum vr_box_base_type base_type,
           size_t offset,
           void *user_data)
{
        struct compare_closure *data = user_data;

        if (!compare_value(data,
                           base_type,
                           data->a + offset,
                           data->b + offset)) {
                data->result = false;
                return false;
        }

        data->index++;

        return true;
}

bool
vr_box_compare(enum vr_box_comparison comparison,
               const struct vr_tolerance *tolerance,
               enum vr_box_type type,
               const struct vr_box_layout *layout,
               const void *a,
               const void *b)
{
        struct compare_closure data = {
                .comparison = comparison,
                .a = a,
                .b = b,
                .tolerance = tolerance,
                .index = 0,
                .index_max = type_infos[type].rows,
                .result = true
        };

        vr_box_for_each_component(type, layout, compare_cb, &data);
        return data.result;
}

const struct vr_box_type_info *
vr_box_type_get_info(enum vr_box_type type)
{
        assert(type >= 0 && type < VR_N_ELEMENTS(type_infos));
        return type_infos + type;
}
