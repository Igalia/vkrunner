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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <math.h>

#include <vkrunner/vkrunner.h>

struct string_array {
        char **data;
        size_t length;
        size_t size;
};

struct main_data {
        struct vr_executor *executor;
        struct vr_config *config;
        const char *image_filename;
        const char *buffer_filename;
        struct string_array filenames;
        struct string_array token_replacements;
        int binding;
        bool inspect_failed;
        bool quiet;
};

typedef bool (* option_cb_t) (struct main_data *data,
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
opt_help(struct main_data *data,
         const char *arg);

static void
string_array_add(struct string_array *array,
                 const char *value)
{
        if (array->length >= array->size) {
                if (array->size == 0) {
                        array->size = 4;
                        array->data = malloc(array->size * sizeof (char *));
                } else {
                        array->size *= 2;
                        array->data = realloc(array->data,
                                              array->size * sizeof (char *));
                }
        }

        size_t len = strlen(value);
        char *str = malloc(len + 1);
        memcpy(str, value, len + 1);
        array->data[array->length++] = str;
}

static void
string_array_destroy(struct string_array *array)
{
        for (size_t i = 0; i < array->length; i++)
                free(array->data[i]);
        if (array->data)
                free(array->data);
}

static bool
opt_image(struct main_data *data,
          const char *arg)
{
        data->image_filename = arg;
        return true;
}

static bool
opt_buffer(struct main_data *data,
           const char *arg)
{
        data->buffer_filename = arg;
        return true;
}

static bool
opt_binding(struct main_data *data,
            const char *arg)
{
        data->binding = strtoul(arg, NULL, 0);
        return true;
}

static bool
opt_disassembly(struct main_data *data,
                const char *arg)
{
        vr_config_set_show_disassembly(data->config, true);
        return true;
}

static bool
opt_token_replacement(struct main_data *data,
                      const char *arg)
{
        const char *equals = strchr(arg, '=');

        if (equals == NULL || equals == arg) {
                fprintf(stderr,
                        "invalid token replacement “%s”\n",
                        arg);
                return false;
        }

        char *token = malloc(equals - arg + 1);
        memcpy(token, arg, equals - arg);
        token[equals - arg] = '\0';

        string_array_add(&data->token_replacements, token);
        string_array_add(&data->token_replacements, equals + 1);

        free(token);

        return true;
}

static bool
opt_quiet(struct main_data *data,
          const char *arg)
{
        data->quiet = true;

        return true;
}

static const struct option
options[] = {
        { 'h', "Show this help message", NULL, opt_help },
        { 'i', "Write the final rendering to IMG as a PPM image", "IMG",
          opt_image },
        { 'b', "Dump contents of a UBO or SSBO to BUF", "BUF",
          opt_buffer },
        { 'B', "Select which buffer to dump using the -b option. "
          "Defaults to first buffer", "BINDING",
          opt_binding },
        { 'd', "Show the SPIR-V disassembly", NULL, opt_disassembly },
        { 'D', "Replace occurences of TOK with REPL in the scripts",
          "TOK=REPL", opt_token_replacement },
        { 'q', "Don’t print any non-error information to stdout", NULL,
          opt_quiet }
};

#define N_OPTIONS (sizeof options / sizeof options[0])

static bool
opt_help(struct main_data *data,
         const char *arg)
{
        printf("usage: vkrunner [OPTION]... SCRIPT...\n"
               "Runs the shader test script SCRIPT\n"
               "\n"
               "Options:\n");

        for (int i = 0; i < N_OPTIONS; i++) {
                printf("  -%c %-10s %s\n",
                       options[i].letter,
                       options[i].argument_name ?
                       options[i].argument_name :
                       "",
                       options[i].description);

        }

        return false;
}

static bool
handle_option(struct main_data *data,
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
                                opt_help(data, NULL);
                                return false;
                        }
                        arg = argv[*arg_num];
                }
        } else {
                arg = NULL;
        }

        return option->cb(data, arg);
}

static bool
process_argv(struct main_data *data,
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
                                     option_num < N_OPTIONS;
                                     option_num++) {
                                        if (options[option_num].letter != *p)
                                                continue;

                                        if (!handle_option(data,
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
                                opt_help(data, NULL);
                                return false;

                        found_option:
                                (void) 0;
                        }

                handled_arg:
                        (void) 0;
                } else {
                        string_array_add(&data->filenames, argv[i]);
                }
        }

        if (data->filenames.length <= 0) {
                fprintf(stderr, "no script specified\n");
                opt_help(data, NULL);
                return false;
        }

        return true;
}

static bool
write_ppm(const struct vr_inspect_image *image,
          const char *filename)
{
        const struct vr_format *format = image->format;
        int format_size = vr_format_get_size(image->format);
        FILE *out = fopen(filename, "wb");

        if (out == NULL) {
                fprintf(stderr,
                        "%s: %s",
                        filename,
                        strerror(errno));
                return false;
        }

        fprintf(out,
                "P6\n"
                "%i %i\n"
                "255\n",
                image->width,
                image->height);

        for (int y = 0; y < image->height; y++) {
                const uint8_t *p = (uint8_t *) image->data + y * image->stride;

                for (int x = 0; x < image->width; x++) {
                        double pixel[4];

                        vr_format_load_pixel(format, p, pixel);

                        for (int i = 0; i < 3; i++) {
                                double v = pixel[i];

                                if (v < 0.0)
                                        v = 0.0;
                                else if (v > 1.0)
                                        v = 1.0;

                                fputc(round(v * 255.0), out);
                        }
                        p += format_size;
                }
        }

        fclose(out);

        return true;
}

static bool
write_buffer(const struct vr_inspect_data *data,
             int binding,
             const char *filename)
{
        const struct vr_inspect_buffer *buffer;

        if (data->n_buffers < 1) {
                fprintf(stderr,
                        "%s: no buffers are used in the test script\n",
                        filename);
                return false;
        }

        if (binding == -1) {
                buffer = data->buffers;
        } else {
                for (int i = 0; i < data->n_buffers; i++) {
                        if (data->buffers[i].binding == binding) {
                                buffer = data->buffers + i;
                                goto found_buffer;
                        }
                }

                fprintf(stderr,
                        "%s: no buffer with binding %i was found\n",
                        filename,
                        binding);
                return false;

        found_buffer:
                (void) 0;
        }

        FILE *out = fopen(filename, "wb");

        if (out == NULL) {
                fprintf(stderr,
                        "%s: %s",
                        filename,
                        strerror(errno));
                return false;
        }

        fwrite(buffer->data, 1, buffer->size, out);
        fclose(out);

        return true;
}

static void
inspect_cb(const struct vr_inspect_data *inspect_data,
           void *user_data)
{
        struct main_data *data = user_data;

        if (data->image_filename) {
                if (!write_ppm(&inspect_data->color_buffer,
                               data->image_filename))
                        data->inspect_failed = true;
        }

        if (data->buffer_filename) {
                if (!write_buffer(inspect_data,
                                  data->binding,
                                  data->buffer_filename))
                        data->inspect_failed = true;
        }
}

static void
add_token_replacements(struct main_data *data,
                       struct vr_source *source)
{
        for (size_t i = 0; i < data->token_replacements.length; i += 2) {
                const char *token = data->token_replacements.data[i];
                const char *val = data->token_replacements.data[i + 1];
                vr_source_add_token_replacement(source, token, val);
        }
}

static enum vr_result
run_scripts(struct main_data *data)
{
        enum vr_result overall_result = VR_RESULT_SKIP;

        for (size_t i = 0; i < data->filenames.length; i++) {
                const char *filename = data->filenames.data[i];

                if (data->filenames.length > 1 && !data->quiet)
                        printf("%s\n", filename);

                struct vr_source *source = vr_source_from_file(filename);

                add_token_replacements(data, source);

                enum vr_result result = vr_executor_execute(data->executor,
                                                            source);
                vr_source_free(source);

                overall_result = vr_result_merge(result, overall_result);
        }

        return overall_result;
}

int
main(int argc, char **argv)
{
        int return_value = EXIT_SUCCESS;
        struct vr_config *config = vr_config_new();
        struct main_data data = {
                .executor = vr_executor_new(config),
                .config = config,
                .filenames = { .data = NULL },
                .token_replacements = { .data = NULL },
                .binding = -1,
                .quiet = false
        };

        vr_config_set_user_data(config, &data);
        vr_config_set_inspect_cb(config, inspect_cb);

        if (process_argv(&data, argc, argv)) {
                enum vr_result result = run_scripts(&data);

                if (data.inspect_failed)
                        result = vr_result_merge(result, VR_RESULT_FAIL);

                if (!data.quiet || result != VR_RESULT_PASS) {
                        printf("PIGLIT: {\"result\": \"%s\" }\n",
                               vr_result_to_string(result));
                }

                if (result != VR_RESULT_PASS)
                        return_value = EXIT_FAILURE;
        } else {
                return_value = EXIT_FAILURE;
        }

        vr_config_free(config);
        vr_executor_free(data.executor);
        string_array_destroy(&data.filenames);
        string_array_destroy(&data.token_replacements);

        return return_value;
}
