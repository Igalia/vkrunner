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

#include "vr-temp-file.h"
#include "vr-util.h"
#include "vr-buffer.h"
#include "vr-error-message.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

bool
vr_temp_file_create_named(const struct vr_config *config,
                          FILE **stream_out,
                          char **filename_out)
{
#ifdef WIN32

        char temp_path[MAX_PATH + 1];

        DWORD len = GetTempPathA(MAX_PATH, temp_path);

        if (len == 0) {
                vr_error_message(config, "GetTempPath failed");
                return false;
        }

        char filename[MAX_PATH + 1];

        DWORD res = GetTempFileNameA(temp_path,
                                     "vkrunner",
                                     0, /* unique */
                                     filename);
        if (res == 0) {
                vr_error_message(config, "GetTempFileName failed");
                return false;
        }

        FILE *stream = fopen(filename, "r+b");
        if (stream == NULL) {
                vr_error_message(config, "%s: %s", filename, strerror(errno));
                return FALSE;
        }

        *filename_out = vr_strdup(filename);
        *stream_out = stream;

#else

        struct vr_buffer filename = VR_BUFFER_STATIC_INIT;

        const char *dir = getenv("TMPDIR");
        if (dir)
                vr_buffer_append_string(&filename, dir);
        else
                vr_buffer_append_string(&filename, "/tmp");

        vr_buffer_append_string(&filename, VR_PATH_SEPARATOR "vkrunner-XXXXXX");

        int fd = mkstemp((char *) filename.data);
        FILE *stream;

        if (fd == -1) {
                vr_error_message(config, "mkstemp: %s", strerror(errno));
                vr_buffer_destroy(&filename);
                return false;
        }

        stream = fdopen(fd, "r+");
        if (stream == NULL) {
                vr_error_message(config,
                                 "%s: %s",
                                 (char *) filename.data,
                                 strerror(errno));
                vr_buffer_destroy(&filename);
                close(fd);
                return false;
        }

        *filename_out = (char *) filename.data;
        *stream_out = stream;

#endif

        return true;
}
