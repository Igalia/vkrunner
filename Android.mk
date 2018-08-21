#
# vkrunner
#
# Copyright (C) 2018 Google LLC
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

LOCAL_PATH := $(call my-dir)

VKRUNNER_SRC_FILES := vkrunner/vr-allocate-store.c \
	vkrunner/vr-box.c \
	vkrunner/vr-buffer.c \
	vkrunner/vr-config.c \
	vkrunner/vr-context.c \
	vkrunner/vr-error-message.c \
	vkrunner/vr-executor.c \
	vkrunner/vr-feature-offsets.c \
	vkrunner/vr-flush-memory.c \
	vkrunner/vr-format.c \
	vkrunner/vr-half-float.c \
	vkrunner/vr-hex.c \
	vkrunner/vr-list.c \
	vkrunner/vr-pipeline.c \
	vkrunner/vr-pipeline-key.c \
	vkrunner/vr-result.c \
	vkrunner/vr-script.c \
	vkrunner/vr-source.c \
	vkrunner/vr-subprocess.c \
	vkrunner/vr-temp-file.c \
	vkrunner/vr-test.c \
	vkrunner/vr-util.c \
	vkrunner/vr-vbo.c \
	vkrunner/vr-vk.c \
	vkrunner/vr-window.c

define gen_config_h
$(call generate-file-dir,$(LOCAL_PATH)/config.h)
$(LOCAL_PATH)/config.h: $(LOCAL_PATH)/config.h.android
	@cp $(LOCAL_PATH)/config.h.android $(LOCAL_PATH)/config.h
	@echo "$(LOCAL_PATH)/config.h <= $(LOCAL_PATH)/config.h.android"
$(foreach F,$(VKRUNNER_SRC_FILES),$(LOCAL_PATH)/$F ) : $(LOCAL_PATH)/config.h
endef
$(eval $(call gen_config_h))

include $(CLEAR_VARS)

LOCAL_MODULE    := vkrunner
LOCAL_SRC_FILES := $(VKRUNNER_SRC_FILES)
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/vkrunner
LOCAL_CFLAGS	 := -Wall -Wuninitialized -Wempty-body -Wformat \
	-Wformat-security -Winit-self -Wundef \
	-Wvla -Wpointer-arith -Wmissing-declarations

include $(BUILD_STATIC_LIBRARY)
