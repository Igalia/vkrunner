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

#include "gui-worker.h"

#include <vkrunner/vkrunner.h>
#include <glib.h>
#include <stdbool.h>
#include <math.h>
#include <cairo.h>

struct gui_worker {
        GMutex mutex;
        GCond cond;
        GThread *thread;

        gui_worker_cb callback;
        void *user_data;

        /* Needs the lock to access */

        bool quit;

        uint64_t pending_serial;
        char *pending_source;
        size_t pending_source_size;
        bool source_is_pending;

        GString *log;
        uint64_t serial_id;
        enum vr_result result;
        guint idle_source;
        cairo_surface_t *image;

        /* Owned by the worker thread, doesn’t need lock */

        GString *next_log;
        cairo_surface_t *next_image;
};

static void
error_cb(const char *message,
         void *user_data)
{
        struct gui_worker *worker = user_data;

        g_string_append(worker->next_log, message);
        g_string_append_c(worker->next_log, '\n');
}

static void
inspect_cb(const struct vr_inspect_data *data,
           void *user_data)
{
        struct gui_worker *worker = user_data;

        if (worker->next_image) {
                worker->image = worker->next_image;
                worker->next_image = NULL;
        }

        const struct vr_inspect_image *image = &data->color_buffer;
        const struct vr_format *format = image->format;
        int format_size = vr_format_get_size(format);

        cairo_surface_t *surface =
                cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                           image->width,
                                           image->height);
        int dst_stride = cairo_image_surface_get_stride(surface);
        uint32_t *dst = (uint32_t *) cairo_image_surface_get_data(surface);

        for (int y = 0; y < image->height; y++) {
                const uint8_t *src = ((uint8_t *) image->data +
                                      y * image->stride);

                for (int x = 0; x < image->width; x++) {
                        double pixel[4];

                        vr_format_load_pixel(format, src, pixel);

                        uint32_t v = 0;

                        for (int i = 0; i < 3; i++) {
                                double c = pixel[i];

                                if (c < 0.0)
                                        c = 0.0;
                                else if (c > 1.0)
                                        c = 1.0;

                                v = (v << 8) | (int) round(c * 255.0);
                        }

                        *(dst++) = v;
                        src += format_size;
                }

                dst += dst_stride / sizeof *dst - image->width;
        }

        worker->next_image = surface;
}

static gboolean
idle_cb(void *user_data)
{
        struct gui_worker *worker = user_data;

        g_mutex_lock(&worker->mutex);

        struct gui_worker_data data = {
                .log = worker->log->str,
                .serial_id = worker->serial_id,
                .result = worker->result,
                .image = worker->image
        };

        worker->callback(&data, worker->user_data);

        worker->idle_source = 0;

        g_mutex_unlock(&worker->mutex);

        return G_SOURCE_REMOVE;
}

static void *
thread_cb(void *user_data)
{
        struct gui_worker *worker = user_data;
        struct vr_config *config = vr_config_new();

        vr_config_set_user_data(config, worker);
        vr_config_set_error_cb(config, error_cb);
        vr_config_set_inspect_cb(config, inspect_cb);

        struct vr_executor *executor = vr_executor_new(config);

        g_mutex_lock(&worker->mutex);

        while (true) {
                g_cond_wait(&worker->cond, &worker->mutex);

                if (worker->quit)
                        break;

                if (!worker->source_is_pending)
                        continue;

                struct vr_source *source =
                        vr_source_from_string(worker->pending_source);
                uint64_t serial_id = worker->pending_serial;

                worker->source_is_pending = false;

                g_mutex_unlock(&worker->mutex);

                g_string_truncate(worker->next_log, 0);

                enum vr_result res = vr_executor_execute(executor, source);

                g_mutex_lock(&worker->mutex);

                g_string_truncate(worker->log, 0);
                g_string_append(worker->log, worker->next_log->str);
                worker->serial_id = serial_id;
                worker->result = res;

                if (worker->image) {
                        cairo_surface_destroy(worker->image);
                        worker->image = NULL;
                }

                if (worker->next_image) {
                        worker->image = worker->next_image;
                        worker->next_image = NULL;
                }

                if (worker->idle_source == 0)
                        worker->idle_source = g_idle_add(idle_cb, worker);
        }

        g_mutex_unlock(&worker->mutex);

        vr_executor_free(executor);
        vr_config_free(config);

        return NULL;
}

struct gui_worker *
gui_worker_new(gui_worker_cb callback,
               void *user_data)
{
        struct gui_worker *worker = g_malloc0(sizeof *worker);

        worker->callback = callback;
        worker->user_data = user_data;
        worker->next_log = g_string_new(NULL);
        worker->log = g_string_new(NULL);

        g_mutex_init(&worker->mutex);
        g_cond_init(&worker->cond);
        worker->thread = g_thread_new("guiworker", thread_cb, worker);

        return worker;
}

void
gui_worker_set_source(struct gui_worker *worker,
                      uint64_t serial_id,
                      const char *source)
{
        g_mutex_lock(&worker->mutex);

        size_t source_length = strlen(source);

        if (worker->pending_source_size < source_length ||
            worker->pending_source_size == 0) {
                worker->pending_source = g_realloc(worker->pending_source,
                                                   source_length + 1);
                worker->pending_source_size = source_length;
        }

        strcpy(worker->pending_source, source);
        worker->source_is_pending = true;
        worker->pending_serial = serial_id;

        g_cond_signal(&worker->cond);

        g_mutex_unlock(&worker->mutex);
}

void
gui_worker_free(struct gui_worker *worker)
{
        g_mutex_lock(&worker->mutex);
        worker->quit = true;
        g_cond_signal(&worker->cond);
        g_mutex_unlock(&worker->mutex);

        g_thread_join(worker->thread);
        g_cond_clear(&worker->cond);
        g_mutex_clear(&worker->mutex);

        g_free(worker->pending_source);

        g_string_free(worker->next_log, true);
        g_string_free(worker->log, true);

        if (worker->image)
                cairo_surface_destroy(worker->image);

        if (worker->idle_source)
                g_source_remove(worker->idle_source);

        g_free(worker);
}
