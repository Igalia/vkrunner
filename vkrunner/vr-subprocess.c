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
#include <ctype.h>

static void
print_lines_in_buffer(const struct vr_config *config,
                      struct vr_buffer *buf)
{
        uint8_t *p = buf->data;

        while (true) {
                uint8_t *end = memchr(p, '\n', buf->data + buf->length - p);

                if (end == NULL)
                        break;

                *end = '\0';

                if (end > p && end[-1] == '\r')
                        end[-1] = '\0';

                vr_error_message_string(config, (const char *) p);

                p = end + 1;
        }

        size_t remaining = buf->data + buf->length - p;
        memmove(buf->data, p, remaining);
        buf->length = remaining;
}

static void
print_remaining_output(const struct vr_config *config,
                       struct vr_buffer *buf)
{
        if (buf->length <= 0)
                return;

        vr_buffer_append_c(buf, '\0');
        vr_error_message_string(config, (const char *) buf->data);
}

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

static bool
create_pipes(const struct vr_config *config,
             HANDLE *stdin_fd_ret,
             HANDLE *stdout_fd_ret,
             HANDLE *stderr_fd_ret,
             HANDLE *output_fd_ret)
{
        SECURITY_ATTRIBUTES sa = {
                .nLength = sizeof sa,
                .bInheritHandle = TRUE
        };

        HANDLE output_fd_tmp = INVALID_HANDLE_VALUE;
        HANDLE output_fd = INVALID_HANDLE_VALUE;
        HANDLE stdin_fd = INVALID_HANDLE_VALUE;
        HANDLE stdout_fd = INVALID_HANDLE_VALUE;
        HANDLE stderr_fd = INVALID_HANDLE_VALUE;

        if (!DuplicateHandle(GetCurrentProcess(),
                             GetStdHandle(STD_INPUT_HANDLE),
                             GetCurrentProcess(),
                             &stdin_fd,
                             0, /* dwDesiredAccess */
                             TRUE, /* bInheritHandle */
                             DUPLICATE_SAME_ACCESS)) {
                stdin_fd = INVALID_HANDLE_VALUE;
                goto error;
        }

        if (!CreatePipe(&output_fd_tmp, &stdout_fd, &sa, 0 /* nSize */)) {
                output_fd_tmp = INVALID_HANDLE_VALUE;
                stdout_fd = INVALID_HANDLE_VALUE;
                goto error;
        }

        /* Duplicate the stderr to the output pipe */
        if (!DuplicateHandle(GetCurrentProcess(),
                             stdout_fd,
                             GetCurrentProcess(),
                             &stderr_fd,
                             0, /* dwDesiredAccess */
                             TRUE, /* bInheritHandle */
                             DUPLICATE_SAME_ACCESS)) {
                stderr_fd = INVALID_HANDLE_VALUE;
                goto error;
        }

        /* Duplicate the read end of the pipe so that we can make it
         * uninheritable
         */
        if (!DuplicateHandle(GetCurrentProcess(),
                             output_fd_tmp,
                             GetCurrentProcess(),
                             &output_fd,
                             0, /* dwDesiredAccess */
                             FALSE, /* bInheritHandle */
                             DUPLICATE_SAME_ACCESS)) {
                output_fd = INVALID_HANDLE_VALUE;
                goto error;
        }

        CloseHandle(output_fd_tmp);
        output_fd_tmp = INVALID_HANDLE_VALUE;

        *stdin_fd_ret = stdin_fd;
        *stdout_fd_ret = stdout_fd;
        *stderr_fd_ret = stderr_fd;
        *output_fd_ret = output_fd;

        return true;

error:
        if (output_fd_tmp != INVALID_HANDLE_VALUE)
                CloseHandle(output_fd_tmp);
        if (output_fd != INVALID_HANDLE_VALUE)
                CloseHandle(output_fd);
        if (stdin_fd != INVALID_HANDLE_VALUE)
                CloseHandle(stdin_fd);
        if (stdout_fd != INVALID_HANDLE_VALUE)
                CloseHandle(stdout_fd);
        if (stderr_fd != INVALID_HANDLE_VALUE)
                CloseHandle(stderr_fd);

        vr_error_message(config, "Error creating pipe");

        return false;
}

static bool
process_output(const struct vr_config *config,
               HANDLE pipe_fd)
{
        struct vr_buffer buf = VR_BUFFER_STATIC_INIT;
        bool ret = true;

        while (true) {
                vr_buffer_ensure_size(&buf, buf.length + 128);

                DWORD got;

                if (!ReadFile(pipe_fd,
                              buf.data + buf.length,
                              buf.size - buf.length,
                              &got,
                              NULL /* lpOverlapped */)) {
                        if (GetLastError() != ERROR_BROKEN_PIPE) {
                                vr_error_message(config, "ReadFile failed");
                                ret = false;
                        }
                        break;
                }

                if (got == 0)
                        break;

                buf.length += got;

                print_lines_in_buffer(config, &buf);
        }

        print_remaining_output(config, &buf);

        vr_buffer_destroy(&buf);

        return ret;
}

bool
vr_subprocess_command(const struct vr_config *config,
                      char * const *arguments)
{
        BOOL res;
        bool result = true;

        HANDLE stdin_fd, stdout_fd, stderr_fd, output_fd;

        if (!create_pipes(config,
                          &stdin_fd,
                          &stdout_fd,
                          &stderr_fd,
                          &output_fd))
                return false;

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
                .dwFlags = STARTF_USESTDHANDLES,
                .hStdInput = stdin_fd,
                .hStdOutput = stdout_fd,
                .hStdError = stderr_fd
        };
        PROCESS_INFORMATION process_info;

        res = CreateProcess(NULL, /* lpApplicationName */
                            (char *) buffer.data,
                            NULL, /* lpProcessAttributes */
                            NULL, /* lpThreadAttributes */
                            TRUE, /* bInheritHandles */
                            0, /* dwCreationFlags */
                            NULL, /* lpEnvironment */
                            NULL, /* lpCurrentDirectory */
                            &startup_info,
                            &process_info);

        CloseHandle(stdin_fd);
        CloseHandle(stdout_fd);
        CloseHandle(stderr_fd);

        vr_buffer_destroy(&buffer);

        if (!res) {
                vr_error_message(config,
                                 "%s: CreateProcess failed",
                                 arguments[0]);
                result = false;
                goto out_pipes;
        }

        if (!process_output(config, output_fd))
                result = false;

        DWORD exit_code;
        if (WaitForSingleObject(process_info.hProcess,
                                INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(process_info.hProcess, &exit_code) ||
            exit_code != 0)
                result = false;

        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);

out_pipes:
        CloseHandle(output_fd);

        return result;
}

#else /* WIN32 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <limits.h>

static bool
process_output(const struct vr_config *config,
               int *pipe_fd,
               struct vr_buffer *buf)
{
        int res;

        vr_buffer_ensure_size(buf, buf->length + 128);

        res = read(*pipe_fd,
                   buf->data + buf->length,
                   buf->size - buf->length);

        if (res < 0) {
                if (errno != EINTR) {
                        vr_error_message(config,
                                         "read: %s\n",
                                         strerror(errno));
                        return false;
                }

                return true;
        }

        if (res == 0) {
                *pipe_fd = -1;
                return true;
        }

        buf->length += res;

        print_lines_in_buffer(config, buf);

        return true;
}

static bool
stream_data(const struct vr_config *config,
            int stdout_pipe,
            int stderr_pipe)
{
        struct vr_buffer stdout_buf = VR_BUFFER_STATIC_INIT;
        struct vr_buffer stderr_buf = VR_BUFFER_STATIC_INIT;
        bool ret = true;

        while (stdout_pipe != -1 || stderr_pipe != -1) {
                int n_pollfds = 0;
                struct pollfd pollfds[2];

                if (stdout_pipe != -1) {
                        pollfds[n_pollfds].fd = stdout_pipe;
                        pollfds[n_pollfds].events = POLLIN;
                        pollfds[n_pollfds].revents = 0;
                        n_pollfds++;
                }

                if (stderr_pipe != -1) {
                        pollfds[n_pollfds].fd = stderr_pipe;
                        pollfds[n_pollfds].events = POLLIN;
                        pollfds[n_pollfds].revents = 0;
                        n_pollfds++;
                }

                int res = poll(pollfds, n_pollfds, INT_MAX);

                if (res < 0) {
                        if (errno == EINTR)
                                continue;
                        vr_error_message(config, "poll: %s\n", strerror(errno));
                        ret = false;
                        goto done;
                }

                for (int i = 0; i < n_pollfds; i++) {
                        if (pollfds[i].revents &
                            ~(POLLIN | POLLOUT | POLLHUP)) {
                                ret = false;
                                goto done;
                        }

                        if (pollfds[i].fd == stdout_pipe) {
                                if (!process_output(config,
                                                    &stdout_pipe,
                                                    &stdout_buf)) {
                                        ret = false;
                                        goto done;
                                }
                        } else if (pollfds[i].fd == stderr_pipe) {
                                if (!process_output(config,
                                                    &stderr_pipe,
                                                    &stderr_buf)) {
                                        ret = false;
                                        goto done;
                                }
                        }
                }
        }

done:
        print_remaining_output(config, &stdout_buf);
        print_remaining_output(config, &stderr_buf);

        vr_buffer_destroy(&stdout_buf);
        vr_buffer_destroy(&stderr_buf);

        return ret;
}

bool
vr_subprocess_command(const struct vr_config *config,
                      char * const *arguments)
{
        pid_t pid;
        int stdout_pipe[2];
        int stderr_pipe[2];

        if (pipe(stdout_pipe) == -1) {
                vr_error_message(config, "pipe: %s\n", strerror(errno));
                return false;
        }
        if (pipe(stderr_pipe) == -1) {
                vr_error_message(config, "pipe: %s\n", strerror(errno));
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);
                return false;
        }

        pid = fork();

        if (pid < 0) {
                vr_error_message(config,
                                 "fork failed: %s\n",
                                 strerror(errno));
                return false;
        } else if (pid == 0) {
                dup2(stdout_pipe[1], STDOUT_FILENO);
                dup2(stderr_pipe[1], STDERR_FILENO);
                for (int i = 3; i < 256; i++)
                        close(i);
                execvp(arguments[0], arguments);
                fprintf(stderr, "%s: %s\n", arguments[0], strerror(errno));
                exit(EXIT_FAILURE);
        } else {
                close(stdout_pipe[1]);
                close(stderr_pipe[1]);

                int status;
                bool ret = stream_data(config,
                                       stdout_pipe[0],
                                       stderr_pipe[0]);

                close(stdout_pipe[0]);
                close(stderr_pipe[0]);

                while (waitpid(pid, &status, 0 /* options */) == -1);

                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                        ret = false;

                return ret;
        }
}

#endif /* WIN32 */
