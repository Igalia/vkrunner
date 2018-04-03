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

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <poll.h>
#include <limits.h>

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
