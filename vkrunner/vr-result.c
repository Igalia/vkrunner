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

#include "vr-result.h"
#include "vr-util.h"

enum vr_result
vr_result_merge(enum vr_result a,
                enum vr_result b)
{
        switch (a) {
        case VR_RESULT_PASS:
                if (b != VR_RESULT_SKIP)
                        return b;
                else
                        return VR_RESULT_PASS;
        case VR_RESULT_FAIL:
                return VR_RESULT_FAIL;
        case VR_RESULT_SKIP:
                return b;
        }

        vr_fatal("Unknown vr_result");
}

const char *
vr_result_to_string(enum vr_result res)
{
        switch (res) {
        case VR_RESULT_FAIL:
                return "fail";
        case VR_RESULT_SKIP:
                return "skip";
        case VR_RESULT_PASS:
                return "pass";
        }

        vr_fatal("Unknown vr_result");
}
