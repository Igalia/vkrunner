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

#ifndef GUI_WORKER_H
#define GUI_WORKER_H

#include <stdint.h>
#include <vkrunner/vkrunner.h>

struct gui_worker;

struct gui_worker_data {
        const char *log;
        uint64_t serial_id;
        enum vr_result result;
};

typedef void (* gui_worker_cb)(const struct gui_worker_data *data,
                               void *user_data);

struct gui_worker *
gui_worker_new(gui_worker_cb callback,
               void *user_data);

void
gui_worker_set_source(struct gui_worker *worker,
                      uint64_t serial_id,
                      const char *source);

void
gui_worker_free(struct gui_worker *worker);

#endif /* GUI_WORKER_H */
