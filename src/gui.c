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

#include <stdlib.h>
#include <gtk/gtk.h>

#include "gui-worker.h"
#include "gui-initial-script.h"

struct gui {
        GtkWidget *window;
        GtkTextBuffer *text_buffer;
        GtkTextBuffer *log_buffer;
        GtkWidget *image_view;
        GtkWidget *statusbar;
        guint result_context_id;
        guint timeout_source;

        struct gui_worker *worker;

        uint64_t serial_id;
};

static void
worker_cb(const struct gui_worker_data *data,
          void *user_data)
{
        struct gui *gui = user_data;

        if (data->serial_id >= gui->serial_id) {
                gtk_statusbar_remove_all(GTK_STATUSBAR(gui->statusbar),
                                         gui->result_context_id);
                char *note = g_strdup_printf("Result: %s",
                                             vr_result_to_string(data->result));
                gtk_statusbar_push(GTK_STATUSBAR(gui->statusbar),
                                   gui->result_context_id,
                                   note);
                g_free(note);
        }

        if (g_utf8_validate(data->log, -1, NULL)) {
                gtk_text_buffer_set_text(gui->log_buffer, data->log, -1);
        } else {
                char *log = g_utf8_make_valid(data->log, -1);
                gtk_text_buffer_set_text(gui->log_buffer, log, -1);
                g_free(log);
        }

        if (data->image) {
                gtk_image_set_from_surface(GTK_IMAGE(gui->image_view),
                                           data->image);
        } else {
                gtk_image_clear(GTK_IMAGE(gui->image_view));
        }
}

static gboolean
timeout_cb(void *user_data)
{
        struct gui *gui = user_data;

        gui->timeout_source = 0;

        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(gui->text_buffer, &start, &end);
        gchar *text = gtk_text_buffer_get_text(gui->text_buffer,
                                               &start, &end,
                                               FALSE /* include hidden */);
        gui_worker_set_source(gui->worker,
                              ++gui->serial_id,
                              text);
        g_free(text);

        return G_SOURCE_REMOVE;
}

static void
text_changed_cb(GtkTextBuffer *buffer,
                void *user_data)
{
        struct gui *gui = user_data;

        if (gui->timeout_source) {
                g_source_remove(gui->timeout_source);
        } else {
                gtk_statusbar_remove_all(GTK_STATUSBAR(gui->statusbar),
                                         gui->result_context_id);
                gtk_statusbar_push(GTK_STATUSBAR(gui->statusbar),
                                   gui->result_context_id,
                                   "Result pending…");
        }

        gui->timeout_source = g_timeout_add(1000, timeout_cb, gui);

}

static struct gui *
gui_new(void)
{
        struct gui *gui = g_malloc0(sizeof *gui);

        gui->window = g_object_ref_sink(gtk_window_new(GTK_WINDOW_TOPLEVEL));
        gtk_window_set_title(GTK_WINDOW(gui->window), "VkRunner");
        gtk_window_set_default_size(GTK_WINDOW(gui->window), 800, 600);
        g_signal_connect(G_OBJECT(gui->window),
                         "destroy",
                         G_CALLBACK(gtk_main_quit),
                         NULL /* user_data */);

        gui->worker = gui_worker_new(worker_cb, gui);

        GtkWidget *grid = gtk_grid_new();
        gtk_orientable_set_orientation(GTK_ORIENTABLE(grid),
                                       GTK_ORIENTATION_VERTICAL);

        GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_paned_set_position(GTK_PANED(hpaned), 400);
        gtk_widget_set_hexpand(hpaned, true);
        gtk_widget_set_vexpand(hpaned, true);

        GtkWidget *scrolled_view = gtk_scrolled_window_new(NULL, NULL);

        gui->text_buffer = gtk_text_buffer_new(NULL);
        g_signal_connect(G_OBJECT(gui->text_buffer),
                         "changed",
                         G_CALLBACK(text_changed_cb),
                         gui);
        GtkWidget *text_view = gtk_text_view_new_with_buffer(gui->text_buffer);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), true);

        gtk_container_add(GTK_CONTAINER(scrolled_view), text_view);

        gtk_paned_pack1(GTK_PANED(hpaned),
                        scrolled_view,
                        true, /* resize */
                        false /* shrink */);

        GtkWidget *vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        gtk_paned_set_position(GTK_PANED(vpaned), 300);
        gtk_widget_set_hexpand(vpaned, true);
        gtk_widget_set_vexpand(vpaned, true);

        gui->image_view = gtk_image_new_from_surface(NULL);

        gtk_paned_pack1(GTK_PANED(vpaned),
                        gui->image_view,
                        true, /* resize */
                        true /* shrink */);

        gui->log_buffer = gtk_text_buffer_new(NULL);
        GtkWidget *log_view = gtk_text_view_new_with_buffer(gui->log_buffer);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), false);
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), true);
        gtk_widget_set_can_focus(log_view, false);

        scrolled_view = gtk_scrolled_window_new(NULL, NULL);

        gtk_container_add(GTK_CONTAINER(scrolled_view), log_view);

        gtk_paned_pack2(GTK_PANED(vpaned),
                        scrolled_view,
                        true, /* resize */
                        false /* shrink */);

        gtk_paned_pack2(GTK_PANED(hpaned),
                        vpaned,
                        true, /* resize */
                        false /* shrink */);

        gtk_grid_attach(GTK_GRID(grid),
                        hpaned,
                        0, 0, /* left / top */
                        1, 1 /* width / height */);

        gui->statusbar = gtk_statusbar_new();
        gtk_widget_set_hexpand(gui->statusbar, true);
        gui->result_context_id =
                gtk_statusbar_get_context_id(GTK_STATUSBAR(gui->statusbar),
                                             "test-result");
        gtk_grid_attach_next_to(GTK_GRID(grid),
                                gui->statusbar,
                                hpaned,
                                GTK_POS_BOTTOM,
                                1, 1 /* width / height */);

        gtk_container_add(GTK_CONTAINER(gui->window), grid);

        gtk_widget_grab_focus(text_view);

        gtk_text_buffer_set_text(gui->text_buffer, gui_initial_script, -1);

        gtk_widget_show_all(gui->window);

        return gui;
}

static void
gui_free(struct gui *gui)
{
        g_object_unref(gui->window);
        g_object_unref(gui->text_buffer);
        g_object_unref(gui->log_buffer);

        gui_worker_free(gui->worker);

        g_free(gui);
}

int
main(int argc, char **argv)
{
        gtk_init(&argc, &argv);

        struct gui *gui = gui_new();

        gtk_main();

        gui_free(gui);

        return EXIT_SUCCESS;
}
