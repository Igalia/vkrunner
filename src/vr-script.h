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

#ifndef VR_SCRIPT_H
#define VR_SCRIPT_H

#include "vr-list.h"
#include "vr-vk.h"
#include "vr-vbo.h"
#include "vr-format.h"

enum vr_script_shader_stage {
        VR_SCRIPT_SHADER_STAGE_VERTEX,
        VR_SCRIPT_SHADER_STAGE_TESS_CTRL,
        VR_SCRIPT_SHADER_STAGE_TESS_EVAL,
        VR_SCRIPT_SHADER_STAGE_GEOMETRY,
        VR_SCRIPT_SHADER_STAGE_FRAGMENT,
        VR_SCRIPT_SHADER_STAGE_COMPUTE
};

#define VR_SCRIPT_N_STAGES 6

enum vr_script_op {
        VR_SCRIPT_OP_DRAW_RECT,
        VR_SCRIPT_OP_DRAW_ARRAYS,
        VR_SCRIPT_OP_PROBE_RECT,
        VR_SCRIPT_OP_SET_PUSH_CONSTANT,
        VR_SCRIPT_OP_SET_UBO_UNIFORM,
        VR_SCRIPT_OP_CLEAR_COLOR,
        VR_SCRIPT_OP_CLEAR
};

enum vr_script_source_type {
        VR_SCRIPT_SOURCE_TYPE_GLSL,
        VR_SCRIPT_SOURCE_TYPE_SPIRV
};

struct vr_script_shader {
        struct vr_list link;
        enum vr_script_source_type source_type;
        size_t length;
        char source[];
};

enum vr_script_type {
        VR_SCRIPT_TYPE_INT,
        VR_SCRIPT_TYPE_UINT,
        VR_SCRIPT_TYPE_INT64,
        VR_SCRIPT_TYPE_UINT64,
        VR_SCRIPT_TYPE_FLOAT,
        VR_SCRIPT_TYPE_DOUBLE,
        VR_SCRIPT_TYPE_VEC2,
        VR_SCRIPT_TYPE_VEC3,
        VR_SCRIPT_TYPE_VEC4,
        VR_SCRIPT_TYPE_DVEC2,
        VR_SCRIPT_TYPE_DVEC3,
        VR_SCRIPT_TYPE_DVEC4,
        VR_SCRIPT_TYPE_IVEC2,
        VR_SCRIPT_TYPE_IVEC3,
        VR_SCRIPT_TYPE_IVEC4,
        VR_SCRIPT_TYPE_UVEC2,
        VR_SCRIPT_TYPE_UVEC3,
        VR_SCRIPT_TYPE_UVEC4,
        VR_SCRIPT_TYPE_I64VEC2,
        VR_SCRIPT_TYPE_I64VEC3,
        VR_SCRIPT_TYPE_I64VEC4,
        VR_SCRIPT_TYPE_U64VEC2,
        VR_SCRIPT_TYPE_U64VEC3,
        VR_SCRIPT_TYPE_U64VEC4,
        VR_SCRIPT_TYPE_MAT2,
        VR_SCRIPT_TYPE_MAT2X3,
        VR_SCRIPT_TYPE_MAT2X4,
        VR_SCRIPT_TYPE_MAT3X2,
        VR_SCRIPT_TYPE_MAT3,
        VR_SCRIPT_TYPE_MAT3X4,
        VR_SCRIPT_TYPE_MAT4X2,
        VR_SCRIPT_TYPE_MAT4X3,
        VR_SCRIPT_TYPE_MAT4,
        VR_SCRIPT_TYPE_DMAT2,
        VR_SCRIPT_TYPE_DMAT2X3,
        VR_SCRIPT_TYPE_DMAT2X4,
        VR_SCRIPT_TYPE_DMAT3X2,
        VR_SCRIPT_TYPE_DMAT3,
        VR_SCRIPT_TYPE_DMAT3X4,
        VR_SCRIPT_TYPE_DMAT4X2,
        VR_SCRIPT_TYPE_DMAT4X3,
        VR_SCRIPT_TYPE_DMAT4,
};

struct vr_script_value {
        enum vr_script_type type;
        union {
                int i;
                unsigned u;
                int64_t i64;
                uint64_t u64;
                float f;
                double d;
                float vec[4];
                double dvec[4];
                int ivec[4];
                unsigned uvec[4];
                int64_t i64vec[4];
                uint64_t u64vec[4];
                float mat[16];
                double dmat[16];
        };
};

struct vr_script_command {
        enum vr_script_op op;
        int line_num;

        union {
                struct {
                        float x, y, w, h;
                        bool use_patches;
                } draw_rect;

                struct {
                        int n_components;
                        int x, y, w, h;
                        double color[4];
                } probe_rect;

                struct {
                        unsigned ubo;
                        size_t offset;
                        struct vr_script_value value;
                } set_ubo_uniform;

                struct {
                        size_t offset;
                        struct vr_script_value value;
                } set_push_constant;

                struct {
                        float color[4];
                } clear_color;

                struct {
                        VkPrimitiveTopology topology;
                        uint32_t vertex_count;
                        uint32_t instance_count;
                        uint32_t first_vertex;
                        uint32_t first_instance;
                        unsigned patch_size;
                } draw_arrays;
        };
};

struct vr_script {
        char *filename;
        struct vr_list stages[VR_SCRIPT_N_STAGES];
        size_t n_commands;
        struct vr_script_command *commands;
        VkPhysicalDeviceFeatures required_features;
        const char *const *extensions;
        const struct vr_format *framebuffer_format;
        struct vr_vbo *vertex_data;
        uint16_t *indices;
        size_t n_indices;
};

struct vr_script *
vr_script_load(const char *filename);

void
vr_script_free(struct vr_script *script);

size_t
vr_script_type_size(enum vr_script_type type);

#endif /* VR_SCRIPT_H */
