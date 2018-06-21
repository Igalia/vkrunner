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

struct vr_config;

struct vr_config *
vr_config_new(void);

bool
vr_config_process_argv(struct vr_config *config,
                       int argc, char **argv);

void
vr_config_add_script(struct vr_config *config,
                     const char *filename);

void
vr_config_add_token_replacement(struct vr_config *config,
                                const char *token,
                                const char *replacement);

/* Sets a pointer to be passed back to the caller in all of the
 * callback fuctions below.
 */
void
vr_config_set_user_data(struct vr_config *config,
                        void *user_data);

typedef void
(* vr_config_error_cb)(const char *message,
                       void *user_data);

/* Sets a callback that will be invoked whenever a test error is
 * invoked such as a compilation error or a probed value was
 * incorrect.
 */
void
vr_config_set_error_cb(struct vr_config *config,
                       vr_config_error_cb error_cb);

void
vr_config_free(struct vr_config *config);

#endif /* VR_CONFIG_H */
