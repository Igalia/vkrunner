/*
 * vkrunner
 *
 * Copyright (C) 2018 Google LLC
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

#include <android/log.h>

#include "vkrunner/vkrunner.h"

// Android log function wrappers
static const char* kTAG = "VkRunner";
#define LOGI(...) \
  ((void)__android_log_print(ANDROID_LOG_INFO, kTAG, __VA_ARGS__))
#define LOGW(...) \
  ((void)__android_log_print(ANDROID_LOG_WARN, kTAG, __VA_ARGS__))
#define LOGE(...) \
  ((void)__android_log_print(ANDROID_LOG_ERROR, kTAG, __VA_ARGS__))

static void error_cb(const char *message, void *user_data) {
  LOGE("%s", message);
}

static const char *string_script =
#include "string_script.inc"
;

void test() {
  enum vr_result result;
  struct vr_config *config;
  struct vr_executor *executor;

  config = vr_config_new();
  vr_config_set_error_cb(config, error_cb);
  vr_config_add_script_string(config, string_script);

  executor = vr_executor_new();
  result = vr_executor_execute(executor, config);

  vr_executor_free(executor);

  vr_config_free(config);

  LOGI("PIGLIT: {\"result\": \"%s\" }\n",
       vr_result_to_string(result));
}
