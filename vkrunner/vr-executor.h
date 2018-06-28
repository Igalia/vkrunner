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
#include <vkrunner/vr-config.h>
#include <vkrunner/vr-result.h>

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

enum vr_result
vr_executor_execute(struct vr_executor *executor,
                    const struct vr_config *config);

void
vr_executor_free(struct vr_executor *executor);

#ifdef  __cplusplus
}
#endif

#endif /* VR_EXECUTOR_H */
