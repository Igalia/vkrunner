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

#include <stddef.h>
#include <vkrunner/vr-shader-stage.h>
#include <vkrunner/vr-source.h>

#ifdef  __cplusplus
extern "C" {
#endif

enum vr_script_source_type {
        VR_SCRIPT_SOURCE_TYPE_GLSL,
        VR_SCRIPT_SOURCE_TYPE_SPIRV,
        VR_SCRIPT_SOURCE_TYPE_BINARY
};

struct vr_script;

struct vr_script_shader_code {
	enum vr_script_source_type source_type;
	enum vr_shader_stage stage;
	size_t source_length;
	char *source;
};

/* Writes the source code of the GLSL shaders in shaders parameter.
 * The returned integer value said how many of them there are.
 *
 * NOTE: Callers should free shaders[i].source memory manually.
 */
int
vr_script_get_shaders(const struct vr_script *script,
                      const struct vr_source *source,
                      struct vr_script_shader_code *shaders);

void
vr_script_free(struct vr_script *script);

#ifdef  __cplusplus
}
#endif
#endif /* VR_SCRIPT_H */
