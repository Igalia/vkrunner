/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEM, IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "config.h"
#include "vr-subprocess.h"
#include "vr-error-message.h"
#include "vr-buffer.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>

#ifdef WIN32

#include <windows.h>

static bool
needs_quotes(const char *arg)
{
        if (*arg == '\0')
                return true;

        while (*arg) {
                if (!isalnum(*arg) && *arg != '-' && *arg != '.')
                        return true;
                arg++;
        }

        return false;
}

bool
vr_subprocess_command(char * const *arguments)
{
        BOOL res;

        struct vr_buffer buffer = VR_BUFFER_STATIC_INIT;

        for (char * const *arg = arguments; *arg; arg++) {
                if (buffer.length > 0)
                        vr_buffer_append_c(&buffer, ' ');

                if (needs_quotes(*arg)) {
                        vr_buffer_append_c(&buffer, '"');
                        vr_buffer_append_string(&buffer, *arg);
                        vr_buffer_append_c(&buffer, '"');
                } else {
                        vr_buffer_append_string(&buffer, *arg);
                }
        }

        vr_buffer_append_c(&buffer, '\0');

        STARTUPINFO startup_info = {
                .cb = sizeof startup_info,
        };
        PROCESS_INFORMATION process_info;

        res = CreateProcess(NULL, /* lpApplicationName */
                            (char *) buffer.data,
                            NULL, /* lpProcessAttributes */
                            NULL, /* lpThreadAttributes */
                            FALSE, /* bInheritHandles */
                            0, /* dwCreationFlags */
                            NULL, /* lpEnvironment */
                            NULL, /* lpCurrentDirectory */
                            &startup_info,
                            &process_info);

        vr_buffer_destroy(&buffer);

        if (!res) {
                vr_error_message("%s: CreateProcess failed", arguments[0]);
                return false;
        }

        DWORD exit_code;
        bool result = (WaitForSingleObject(process_info.hProcess,
                                           INFINITE) == WAIT_OBJECT_0 &&
                       GetExitCodeProcess(process_info.hProcess, &exit_code) &&
                       exit_code == 0);

        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);

        return result;
}

struct vr_subprocess_sink *
vr_subprocess_open_sink(char *const *arguments)
{
        vr_error_message("Process sinks not supported on Windows");
        return NULL;
}

void
vr_subprocess_close_sink(struct vr_subprocess_sink *sink)
{
}

#else /* WIN32 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

bool
vr_subprocess_command(char * const *arguments)
{
        pid_t pid;

        pid = fork();

        if (pid < 0) {
                vr_error_message("fork failed: %s\n", strerror(errno));
                return false;
        } else if (pid == 0) {
                for (int i = 3; i < 256; i++)
                        close(i);
                execvp(arguments[0], arguments);
                fprintf(stderr, "%s: %s\n", arguments[0], strerror(errno));
                exit(EXIT_FAILURE);
        } else {
                int status;
                while (waitpid(pid, &status, 0 /* options */) == -1);

                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                        return false;
        }

        return true;
}

struct vr_subprocess_sink *
vr_subprocess_open_sink(char * const *arguments)
{
        pid_t pid;
        int stdin_pipe[2];

        if (pipe(stdin_pipe) == -1) {
                vr_error_message("pipe: %s", strerror(errno));
                return NULL;
        }

        pid = fork();

        if (pid < 0) {
                vr_error_message("fork failed: %s", strerror(errno));
                close(stdin_pipe[0]);
                close(stdin_pipe[1]);
                return NULL;
        } else if (pid == 0) {
                dup2(stdin_pipe[0], STDIN_FILENO);
                for (int i = 3; i < 256; i++)
                        close(i);
                execvp(arguments[0], arguments);
                fprintf(stderr, "%s: %s\n", arguments[0], strerror(errno));
                exit(EXIT_FAILURE);
        } else {
                close(stdin_pipe[0]);

                FILE *out = fdopen(stdin_pipe[1], "w");

                if (out == NULL) {
                        vr_error_message("fdopen failed: %s", strerror(errno));
                        close(stdin_pipe[1]);
                        kill(pid, SIGKILL);
                        while (waitpid(pid, NULL, 0) == -1);
                        return NULL;
                }

                struct vr_subprocess_sink *sink = vr_alloc(sizeof *sink);

                sink->pid = pid;
                sink->out = out;

                return sink;
        }
}

void
vr_subprocess_close_sink(struct vr_subprocess_sink *sink)
{
        fclose(sink->out);
        while (waitpid(sink->pid, NULL, 0) == -1);
        vr_free(sink);
}

#endif /* WIN32 */
