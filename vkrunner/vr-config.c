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

#include "vr-config-private.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

typedef bool (* option_cb_t) (struct vr_config *config,
                              const char *arg);

struct option {
        char letter;
        const char *description;
        /* If the option takes an argument then this will be its name
         * in the help. Otherwise it is NULL if the argument is just a
         * flag.
         */
        char *argument_name;
        option_cb_t cb;
};

static bool
opt_help(struct vr_config *config,
         const char *arg);

static bool
opt_image(struct vr_config *config,
          const char *arg)
{
        if (config->image_filename) {
                fprintf(stderr, "duplicate -i option\n");
                return false;
        }

        config->image_filename = vr_strdup(arg);

        return true;
}

static bool
opt_disassembly(struct vr_config *config,
                const char *arg)
{
        config->show_disassembly = true;
        return true;
}

static bool
opt_token_replacement(struct vr_config *config,
                      const char *arg)
{
        const char *equals = strchr(arg, '=');

        if (equals == NULL || equals == arg) {
                fprintf(stderr,
                        "invalid token replacement “%s”\n",
                        arg);
                return false;
        }

        struct vr_config_token_replacement *tr = vr_calloc(sizeof *tr);

        tr->token = vr_strndup(arg, equals - arg);
        tr->replacement = vr_strdup(equals + 1);

        vr_list_insert(config->token_replacements.prev, &tr->link);

        return true;
}

static const struct option
options[] = {
        { 'h', "Show this help message", NULL, opt_help },
        { 'i', "Write the final rendering to IMG as a PPM image", "IMG",
          opt_image },
        { 'd', "Show the SPIR-V disassembly", NULL, opt_disassembly },
        { 'D', "Replace occurences of TOK with REPL in the scripts",
          "TOK=REPL", opt_token_replacement },
};

static bool
opt_help(struct vr_config *config,
         const char *arg)
{
        printf("usage: vkrunner [OPTION]... SCRIPT...\n"
               "Runs the shader test script SCRIPT\n"
               "\n"
               "Options:\n");

        for (int i = 0; i < VR_N_ELEMENTS(options); i++) {
                printf("  -%c %-10s %s\n",
                       options[i].letter,
                       options[i].argument_name ?
                       options[i].argument_name :
                       "",
                       options[i].description);

        }

        return false;
}

void
vr_config_add_script(struct vr_config *config,
                     const char *filename)
{
        struct vr_config_script *script =
                vr_alloc(sizeof *script + strlen(filename) + 1);
        strcpy(script->filename, filename);
        vr_list_insert(config->scripts.prev, &script->link);
}

void
vr_config_add_token_replacement(struct vr_config *config,
                                const char *token,
                                const char *replacement)
{
        struct vr_config_token_replacement *tr = vr_calloc(sizeof *tr);

        tr->token = vr_strdup(token);
        tr->replacement = vr_strdup(replacement);

        vr_list_insert(config->token_replacements.prev, &tr->link);
}

static bool
handle_option(struct vr_config *config,
              const struct option *option,
              const char *p,
              int argc, char **argv,
              int *arg_num)
{
        const char *arg;

        if (option->argument_name) {
                if (p[1]) {
                        arg = p + 1;
                } else {
                        (*arg_num)++;
                        if (*arg_num >= argc) {
                                fprintf(stderr,
                                        "option ‘%c’ expects an argument\n",
                                        *p);
                                opt_help(config, NULL);
                                return false;
                        }
                        arg = argv[*arg_num];
                }
        } else {
                arg = NULL;
        }

        return option->cb(config, arg);
}

bool
vr_config_process_argv(struct vr_config *config,
                       int argc, char **argv)
{
        bool had_separator = false;

        for (int i = 1; i < argc; i++) {
                if (!had_separator && argv[i][0] == '-') {
                        if (!strcmp(argv[i], "--")) {
                                had_separator = true;
                                continue;
                        }

                        for (const char *p = argv[i] + 1; *p; p++) {
                                for (int option_num = 0;
                                     option_num < VR_N_ELEMENTS(options);
                                     option_num++) {
                                        if (options[option_num].letter != *p)
                                                continue;

                                        if (!handle_option(config,
                                                           options + option_num,
                                                           p,
                                                           argc, argv,
                                                           &i))
                                                return false;

                                        if (options[option_num].argument_name)
                                                goto handled_arg;

                                        goto found_option;
                                }

                                fprintf(stderr,
                                        "unknown option ‘%c’\n",
                                        *p);
                                opt_help(config, NULL);
                                return false;

                        found_option:
                                (void) 0;
                        }

                handled_arg:
                        (void) 0;
                } else {
                        vr_config_add_script(config, argv[i]);
                }
        }

        if (vr_list_empty(&config->scripts)) {
                fprintf(stderr, "no script specified\n");
                opt_help(config, NULL);
                return false;
        }

        return true;
}

void
vr_config_set_user_data(struct vr_config *config,
                        void *user_data)
{
        config->user_data = user_data;
}

void
vr_config_set_error_cb(struct vr_config *config,
                       vr_config_error_cb error_cb)
{
        config->error_cb = error_cb;
}

void
vr_config_set_before_test_cb(struct vr_config *config,
                             vr_config_before_test_cb before_test_cb)
{
        config->before_test_cb = before_test_cb;
}

void
vr_config_set_after_test_cb(struct vr_config *config,
                            vr_config_after_test_cb after_test_cb)
{
        config->after_test_cb = after_test_cb;
}

struct vr_config *
vr_config_new(void)
{
        struct vr_config *config = vr_calloc(sizeof *config);

        vr_list_init(&config->scripts);
        vr_list_init(&config->token_replacements);

        return config;
}

static void
free_scripts(struct vr_config *config)
{
        struct vr_config_script *script, *tmp;

        vr_list_for_each_safe(script, tmp, &config->scripts, link)
                vr_free(script);
}

static void
free_token_replacements(struct vr_config *config)
{
        struct vr_config_token_replacement *tr, *tmp;

        vr_list_for_each_safe(tr, tmp, &config->token_replacements, link) {
                vr_free(tr->token);
                vr_free(tr->replacement);
                vr_free(tr);
        }
}

void
vr_config_free(struct vr_config *config)
{
        free_scripts(config);
        free_token_replacements(config);

        vr_free(config->image_filename);

        vr_free(config);
}
