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

#ifndef VR_SCRIPT_PRIVATE_H
#define VR_SCRIPT_PRIVATE_H

#include "vr-script.h"
#include "vr-list.h"
#include "vr-vk.h"
#include "vr-vbo.h"
#include "vr-format.h"
#include "vr-pipeline-key.h"
#include "vr-config-private.h"
#include "vr-box.h"
#include "vr-source.h"
#include "vr-shader-stage.h"
#include "vr-tolerance.h"
#include "vr-window-format.h"
#include "vr-requirements.h"

enum vr_script_op {
        VR_SCRIPT_OP_DRAW_RECT,
        VR_SCRIPT_OP_DRAW_ARRAYS,
        VR_SCRIPT_OP_DISPATCH_COMPUTE,
        VR_SCRIPT_OP_PROBE_RECT,
        VR_SCRIPT_OP_PROBE_SSBO,
        VR_SCRIPT_OP_SET_PUSH_CONSTANT,
        VR_SCRIPT_OP_SET_BUFFER_SUBDATA,
        VR_SCRIPT_OP_CLEAR
};

struct vr_script_command {
        size_t line_num;
        enum vr_script_op op;

        union {
                struct {
                        float x, y, w, h;
                        size_t pipeline_key;
                } draw_rect;

                struct {
                        unsigned x, y, z;
                        size_t pipeline_key;
                } dispatch_compute;

                struct {
                        int n_components;
                        int x, y, w, h;
                        double color[4];
                        struct vr_tolerance tolerance;
                } probe_rect;

                struct {
                        unsigned desc_set;
                        unsigned binding;
                        enum vr_box_comparison comparison;
                        size_t offset;
                        enum vr_box_type type;
                        struct vr_box_layout layout;
                        void *value;
                        size_t value_size;
                        struct vr_tolerance tolerance;
                } probe_ssbo;

                struct {
                        unsigned desc_set;
                        unsigned binding;
                        size_t offset;
                        void *data;
                        size_t size;
                } set_buffer_subdata;

                struct {
                        size_t offset;
                        void *data;
                        size_t size;
                } set_push_constant;

                struct {
                        float color[4];
                        float depth;
                        uint32_t stencil;
                } clear;

                struct {
                        VkPrimitiveTopology topology;
                        bool indexed;
                        uint32_t vertex_count;
                        uint32_t instance_count;
                        uint32_t first_vertex;
                        uint32_t first_instance;
                        size_t pipeline_key;
                } draw_arrays;
        };
};

enum vr_script_buffer_type {
        VR_SCRIPT_BUFFER_TYPE_UBO,
        VR_SCRIPT_BUFFER_TYPE_SSBO,
};

struct vr_script_buffer {
        unsigned desc_set;
        unsigned binding;
        enum vr_script_buffer_type type;
        size_t size;
};

char *
vr_script_get_filename(const struct vr_script *script);

void
vr_script_get_commands(const struct vr_script *script,
                       const struct vr_script_command **commands_out,
                       size_t *n_commands_out);

size_t
vr_script_get_n_pipeline_keys(const struct vr_script *script);

const struct vr_pipeline_key *
vr_script_get_pipeline_key(const struct vr_script *script,
                           size_t key_num);

const struct vr_requirements *
vr_script_get_requirements(const struct vr_script *script);

const struct vr_window_format *
vr_script_get_window_format(const struct vr_script *script);

const struct vr_vbo *
vr_script_get_vertex_data(const struct vr_script *script);

void
vr_script_get_indices(const struct vr_script *script,
                      const uint16_t **indices_out,
                      size_t *n_indices_out);

void
vr_script_get_buffers(const struct vr_script *script,
                      const struct vr_script_buffer **buffers_out,
                      size_t *n_buffers_out);

#endif /* VR_SCRIPT_PRIVATE_H */
