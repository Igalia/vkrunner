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

#ifndef VR_EXECUTOR_H
#define VR_EXECUTOR_H

#include <stdbool.h>
#include <vkrunner/vr-result.h>
#include <vkrunner/vr-source.h>
#include <vkrunner/vr-callback.h>
#include <vkrunner/vr-script.h>

struct vr_executor;

typedef void *
(* vr_executor_get_instance_proc_cb)(const char *name,
                                     void *user_data);

#ifdef  __cplusplus
extern "C" {
#endif

struct vr_executor *
vr_executor_new(void);

/* Sets an externally created device to use for the execution. Note
 * that it is the callers responsibility to ensure that the device has
 * all the necessary features and extensions enabled for the tests.
 *
 * get_instance_proc_cb is a pointer to a function which will be used
 * to retrieve all of the instance-level functions of the Vulkan API.
 * The device-level functions will be retrieved via
 * vkGetDeviceProcAddr which will itself be retrieved via
 * get_instance_proc_cb. The callback will be passed the user_data
 * pointer which it can use to access internal state.
 *
 * This function is optional and if it is not used then VkRunner will
 * search for an appropriate physical device and create the VkDevice
 * itself.
 */
void
vr_executor_set_device(struct vr_executor *executor,
                       vr_executor_get_instance_proc_cb get_instance_proc_cb,
                       void *user_data,
                       /* VkPhysicalDevice */
                       void *physical_device,
                       int queue_family,
                       /* VkDevice */
                       void *device);

void
vr_executor_set_show_disassembly(struct vr_executor *executor,
                                 bool show_disassembly);

/* Sets a pointer to be passed back to the caller in all of the
 * callback fuctions below.
 */
void
vr_executor_set_user_data(struct vr_executor *executor,
                          void *user_data);

/* Sets a callback that will be invoked whenever a test error is
 * invoked such as a compilation error or a probed value was
 * incorrect.
 */
void
vr_executor_set_error_cb(struct vr_executor *executor,
                         vr_callback_error error_cb);

/* Sets a callback to invoke after the commands in the test section
 * have run. It is not invoked if the test fails before the test
 * section is reached. The application can use the inspect struct to
 * query the buffers used by the test.
 */
void
vr_executor_set_inspect_cb(struct vr_executor *executor,
                           vr_callback_inspect inspect_cb);

enum vr_result
vr_executor_execute(struct vr_executor *executor,
                    const struct vr_source *source);

void
vr_executor_free(struct vr_executor *executor);

#ifdef  __cplusplus
}
#endif

#endif /* VR_EXECUTOR_H */
