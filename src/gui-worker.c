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

        /* Owned by the worker thread, doesn’t need lock */

        GString *next_log;
};

static void
error_cb(const char *message,
         void *user_data)
{
        struct gui_worker *worker = user_data;

        g_string_append(worker->next_log, message);
        g_string_append_c(worker->next_log, '\n');
}

static gboolean
idle_cb(void *user_data)
{
        struct gui_worker *worker = user_data;

        g_mutex_lock(&worker->mutex);

        struct gui_worker_data data = {
                .log = worker->log->str,
                .serial_id = worker->serial_id,
                .result = worker->result
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

        if (worker->idle_source)
                g_source_remove(worker->idle_source);

        g_free(worker);
}
