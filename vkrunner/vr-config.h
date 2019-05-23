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

#ifndef VR_CONFIG_H
#define VR_CONFIG_H

#include <stdbool.h>
#include <vkrunner/vr-callback.h>

#ifdef  __cplusplus
extern "C" {
#endif

struct vr_config;

/* Create a new vr_config object */
struct vr_config *
vr_config_new(void);

/* Free a vr_config object */
void
vr_config_free(struct vr_config *config);

void
vr_config_set_show_disassembly(struct vr_config *config,
                               bool show_disassembly);

/* Sets a pointer to be passed back to the caller in all of the
 * callback fuctions below.
 */
void
vr_config_set_user_data(struct vr_config *config,
                        void *user_data);

/* Sets a callback that will be invoked whenever a test error is
 * invoked such as a compilation error or a probed value was
 * incorrect.
 */
void
vr_config_set_error_cb(struct vr_config *config,
                       vr_callback_error error_cb);

/* Sets a callback to invoke after the commands in the test section
 * have run. It is not invoked if the test fails before the test
 * section is reached. The application can use the inspect struct to
 * query the buffers used by the test.
 */
void
vr_config_set_inspect_cb(struct vr_config *config,
                         vr_callback_inspect inspect_cb);

void
vr_config_set_device_id(struct vr_config *config,
                        int device_id);

#ifdef  __cplusplus
}
#endif
#endif /* VR_CONFIG_H */
