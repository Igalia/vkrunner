/*
 * Copyright © 2011, 2016, 2018 Intel Corporation
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

/* Based on piglit-vbo.cpp */

/**
 * This file adds the facility for specifying vertex data to piglit
 * tests using a columnar text format, for example:
 *
 *   \verbatim
 *   0/r32g32b32_sfloat 1/r32_uint      3/int/int       4/int/int
 *   0.0 0.0 0.0        10              0               0       # comment
 *   0.0 1.0 0.0         5              1               1
 *   1.0 1.0 0.0         0              0               1
 *   \endverbatim
 *
 * The format consists of a row of column headers followed by any
 * number of rows of data. Each column header has the form
 * ATTRLOC/FORMAT where ATTRLOC is the location of the vertex
 * attribute to be bound to this column and FORMAT is the name of a
 * VkFormat minus the VK_FORMAT prefix.
 *
 * Alternatively the column header can use something closer the Piglit
 * format like ATTRLOC/GL_TYPE/GLSL_TYPE. GL_TYPE is the GL type of
 * data that follows (“half”, “float”, “double”, “byte”, “ubyte”,
 * “short”, “ushort”, “int” or “uint”), GLSL_TYPE is the GLSL type of
 * the data (“int”, “uint”, “float”, “double”, “ivec”\*, “uvec”\*,
 * “vec”\*, “dvec”\*).
 *
 * The data follows the column headers in space-separated form. “#”
 * can be used for comments, as in shell scripts.
 */

#include "config.h"

#include "vr-vbo.h"
#include "vr-util.h"
#include "vr-error-message.h"
#include "vr-hex.h"
#include "vr-buffer.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * Convert from Piglit style formats to a VkFormat
 */
static const struct vr_format *
decode_type(const char *gl_type,
            const char *glsl_type)
{
        static const struct {
                const char *name;
                enum vr_format_mode mode;
                int bit_size;
        } gl_types[] = {
                { "byte", VR_FORMAT_MODE_SINT, 8 },
                { "ubyte", VR_FORMAT_MODE_UINT, 8 },
                { "short", VR_FORMAT_MODE_SINT, 16 },
                { "ushort", VR_FORMAT_MODE_UINT, 16 },
                { "int", VR_FORMAT_MODE_SINT, 32 },
                { "uint", VR_FORMAT_MODE_UINT, 32 },
                { "half", VR_FORMAT_MODE_SFLOAT, 16 },
                { "float", VR_FORMAT_MODE_SFLOAT, 32 },
                { "double", VR_FORMAT_MODE_SFLOAT, 64 },
        };

        enum vr_format_mode mode;
        int bit_size;
        int n_components;

        for (int i = 0; i < VR_N_ELEMENTS(gl_types); i++) {
                if (!strcmp(gl_types[i].name, gl_type)) {
                        mode = gl_types[i].mode;
                        bit_size = gl_types[i].bit_size;
                        goto found_gl_type;
                }
        }

        vr_error_message("Unknown gl_type: %s", gl_type);
        return NULL;

found_gl_type:
        if (!strcmp(glsl_type, "int") ||
            !strcmp(glsl_type, "uint") ||
            !strcmp(glsl_type, "float") ||
            !strcmp(glsl_type, "double")) {
                n_components = 1;
        } else {
                if (!strncmp(glsl_type, "vec", 3)) {
                        n_components = glsl_type[3] - '0';
                } else if (strchr("iud", glsl_type[0]) &&
                           !strncmp(glsl_type + 1, "vec", 3)) {
                        n_components = glsl_type[4] - '0';
                } else {
                        vr_error_message("Unknown glsl_type: %s",
                                         glsl_type);
                        return NULL;
                }

                if (n_components < 2 || n_components > 4) {
                        vr_error_message("Invalid components: %s",
                                         glsl_type);
                        return NULL;
                }
        }

        const struct vr_format *format =
                vr_format_lookup_by_details(bit_size,
                                            mode,
                                            n_components);

        if (format == NULL) {
                vr_error_message("Invalid type combo: %s/%s",
                                 gl_type,
                                 glsl_type);
                return NULL;
        }

        return format;
}

static bool
get_attrib_location(const char *name,
                    unsigned *index)
{
        errno = 0;

        char *end;
        unsigned long ul = strtoul(name, &end, 10);

        if (errno == 0 && end > name && ul <= UINT_MAX) {
                *index = ul;
                return true;
        } else {
                return false;
        }
}

/**
 * Build a vertex_attrib_description from a column header, by
 * interpreting the location, type, dimensions and mattrix_column
 * parts of the header.
 *
 * If there is a parse failure, print a description of the problem and
 * then return false.
 */
static bool
parse_vertex_attrib(struct vr_vbo_attrib *attrib,
                    const char *text)
{
        char *name = NULL;
        bool ret = true;

        /* Split the column header into location/type/dimensions
         * fields.
         */
        const char *first_slash = strchr(text, '/');
        if (first_slash == NULL) {
                vr_error_message("Column headers must be in the form"
                                 " location/format.\n"
                                 "Got: %s",
                                 text);
                ret = false;
                goto out;
        }

        size_t name_size = first_slash - text;
        name = vr_strndup(text, name_size);

        const struct vr_format *format;

        const char *second_slash = strchr(first_slash + 1, '/');
        if (second_slash == NULL) {
                format = vr_format_lookup_by_name(first_slash + 1);
                if (format == NULL) {
                        vr_error_message("Unknown format: %s", first_slash + 1);
                        ret = false;
                        goto out;
                }
        } else {
                char *gl_type = vr_strndup(first_slash + 1,
                                           second_slash - first_slash - 1);
                format = decode_type(gl_type, second_slash + 1);
                vr_free(gl_type);

                if (format == NULL) {
                        ret = false;
                        goto out;
                }
        }

        if (!get_attrib_location(name, &attrib->location)) {
                vr_error_message("Unexpected vbo column name.  Got: %s",
                                 name);
                ret = false;
                goto out;
        }

        attrib->format = format;

out:
        vr_free(name);

        return ret;
}

/**
 * Parse a single number (floating point or integral) from one of the
 * data rows, and store it in the location pointed to by \c data.
 * Update \c text to point to the next character of input.
 *
 * If there is a parse failure, print a description of the problem and
 * then return false.  Otherwise return true.
 */
static bool
parse_datum(enum vr_format_mode mode,
            int bit_size,
            const char **text,
            void *data)
{
        char *endptr;
        errno = 0;
        switch (mode) {
        case VR_FORMAT_MODE_SFLOAT:
                switch (bit_size) {
                case 16: {
                        unsigned short value = vr_hex_strtohf(*text, &endptr);
                        if (errno == ERANGE) {
                                vr_error_message("Could not parse as "
                                                 "half float");
                                return false;
                        }
                        *((uint16_t *) data) = value;
                        goto handled;
                }
                case 32: {
                        float value = vr_hex_strtof(*text, &endptr);
                        if (errno == ERANGE) {
                                vr_error_message("Could not parse as float");
                                return false;
                        }
                        *((float *) data) = value;
                        goto handled;
                }
                case 64: {
                        double value = vr_hex_strtod(*text, &endptr);
                        if (errno == ERANGE) {
                                vr_error_message("Could not parse as double");
                                return false;
                        }
                        *((double *) data) = value;
                        goto handled;
                }
                }
        case VR_FORMAT_MODE_UNORM:
        case VR_FORMAT_MODE_USCALED:
        case VR_FORMAT_MODE_UINT:
        case VR_FORMAT_MODE_SRGB:
                switch (bit_size) {
                case 8: {
                        unsigned long value = strtoul(*text, &endptr, 0);
                        if (errno == ERANGE || value > UINT8_MAX) {
                                vr_error_message("Could not parse as unsigned "
                                                 "byte");
                                return false;
                        }
                        *((uint8_t *) data) = (uint8_t) value;
                        goto handled;
                }
                case 16: {
                        unsigned long value = strtoul(*text, &endptr, 0);
                        if (errno == ERANGE || value > UINT16_MAX) {
                                vr_error_message("Could not parse as unsigned "
                                                 "short");
                                return false;
                        }
                        *((uint16_t *) data) = (uint16_t) value;
                        goto handled;
                }
                case 32: {
                        unsigned long value = strtoul(*text, &endptr, 0);
                        if (errno == ERANGE || value > UINT32_MAX) {
                                vr_error_message("Could not parse as "
                                                 "unsigned integer");
                                return false;
                        }
                        *((uint32_t *) data) = (uint32_t) value;
                        goto handled;
                }
                case 64: {
                        unsigned long value = strtoul(*text, &endptr, 0);
                        if (errno == ERANGE || value > UINT64_MAX) {
                                vr_error_message("Could not parse as "
                                                 "unsigned long");
                                return false;
                        }
                        *((uint64_t *) data) = (uint64_t) value;
                        goto handled;
                }
                }
        case VR_FORMAT_MODE_SNORM:
        case VR_FORMAT_MODE_SSCALED:
        case VR_FORMAT_MODE_SINT:
                switch (bit_size) {
                case 8: {
                        long value = strtol(*text, &endptr, 0);
                        if (errno == ERANGE ||
                            value > INT8_MAX || value < INT8_MIN) {
                                vr_error_message("Could not parse as signed "
                                                 "byte");
                                return false;
                        }
                        *((int8_t *) data) = (int8_t) value;
                        goto handled;
                }
                case 16: {
                        long value = strtol(*text, &endptr, 0);
                        if (errno == ERANGE ||
                            value > INT16_MAX || value < INT16_MIN) {
                                vr_error_message("Could not parse as signed "
                                                 "short");
                                return false;
                        }
                        *((int16_t *) data) = (int16_t) value;
                        goto handled;
                }
                case 32: {
                        long value = strtol(*text, &endptr, 0);
                        if (errno == ERANGE ||
                            value > INT32_MAX || value < INT32_MIN) {
                                vr_error_message("Could not parse as "
                                                 "signed integer");
                                return false;
                        }
                        *((int32_t *) data) = (int32_t) value;
                        goto handled;
                }
                case 64: {
                        long value = strtol(*text, &endptr, 0);
                        if (errno == ERANGE ||
                            value > INT64_MAX || value < INT64_MIN) {
                                vr_error_message("Could not parse as "
                                                 "signed long");
                                return false;
                        }
                        *((int64_t *) data) = (int64_t) value;
                        goto handled;
                }
                }
        case VR_FORMAT_MODE_UFLOAT:
                break;
        }

        vr_fatal("Unexpected format");

handled:
        *text = endptr;

        return true;
}

struct vbo_data {
        /**
         * True if the header line has already been parsed.
         */
        bool header_seen;

        struct vr_buffer raw_data;

        struct vr_vbo *vbo;

        unsigned line_num;
};

static int
get_alignment(const struct vr_format *format)
{
        if (format->packed_size)
                return format->packed_size / 8;

        int max_size = 8;

        for (int i = 0; i < format->n_components; i++) {
                if (format->components[i].bits > max_size)
                        max_size = format->components[i].bits;
        }

        return max_size / 8;
}

/**
 * Populate this->attribs and compute this->stride based on column
 * headers.
 *
 * If there is a parse failure, print a description of the problem and
 * then return false
 */
static bool
parse_header_line(struct vbo_data *data,
                  const char *line)
{
        struct vr_vbo *vbo = data->vbo;
        size_t pos = 0;
        size_t line_size;
        const char *newline = strchr(line, '\n');

        if (newline)
                line_size = newline - line;
        else
                line_size = strlen(line);

        vbo->stride = 0;

        int max_alignment = 1;

        while (pos < line_size) {
                if (isspace(line[pos])) {
                        ++pos;
                        continue;
                }

                size_t column_header_end = pos;
                while (column_header_end < line_size &&
                       !isspace(line[column_header_end]))
                        ++column_header_end;

                char *column_header =
                        vr_strndup(line + pos, column_header_end - pos);

                struct vr_vbo_attrib *attrib =
                        vr_calloc(sizeof *attrib);
                vr_list_insert(vbo->attribs.prev, &attrib->link);

                bool res = parse_vertex_attrib(attrib, column_header);

                vr_free(column_header);

                if (!res)
                        return false;

                int alignment = get_alignment(attrib->format);

                vbo->stride = vr_align(vbo->stride, alignment);
                attrib->offset = vbo->stride;
                vbo->stride += vr_format_get_size(attrib->format);
                pos = column_header_end + 1;

                if (alignment > max_alignment)
                        max_alignment = alignment;
        }

        vbo->stride = vr_align(vbo->stride, max_alignment);

        return true;
}


/**
 * Convert a data row into binary form and append it to data->raw_data.
 *
 * If there is a parse failure, print a description of the problem and
 * then return false.
 */
static bool
parse_data_line(struct vbo_data *data,
                const char *line)
{
        struct vr_vbo *vbo = data->vbo;

        /* Allocate space in raw_data for this line */
        size_t old_length = data->raw_data.length;
        vr_buffer_set_length(&data->raw_data, old_length + vbo->stride);

        const char *line_ptr = line;
        const struct vr_vbo_attrib *attrib;

        vr_list_for_each(attrib, &vbo->attribs, link) {
                uint8_t *data_ptr = (data->raw_data.data +
                                     old_length +
                                     attrib->offset);

                if (attrib->format->packed_size) {
                        if (!parse_datum(VR_FORMAT_MODE_UINT,
                                         attrib->format->packed_size,
                                         &line_ptr,
                                         data_ptr))
                                goto error;
                        continue;
                }

                for (size_t j = 0; j < attrib->format->n_components; ++j) {
                        if (!parse_datum(attrib->format->mode,
                                         attrib->format->components[j].bits,
                                         &line_ptr,
                                         data_ptr))
                                goto error;

                        data_ptr += attrib->format->components[j].bits / 8;
                }
        }

        ++vbo->num_rows;

        return true;

error:
        vr_error_message("At line %u of [vertex data] section. "
                         "Offending text: %s",
                         data->line_num,
                         line_ptr);
        return false;
}


/**
 * Parse a line of input text.
 *
 * If there is a parse failure, print a description of the problem and
 * then return false
 */
static bool
parse_line(struct vbo_data *data,
           const char *line,
           const char *text_end)
{
        while (line < text_end && *line != '\n' && isspace(*line))
                line++;

        const char *line_end;

        for (line_end = line;
             /* Ignore end-of-line comments */
             (line_end < text_end &&
              *line_end != '#' &&
              *line_end != '\n' &&
              *line_end);
             line_end++);

        /* Ignore blank or comment-only lines */
        if (line_end == line)
                return true;

        char *line_copy = vr_strndup(line, line_end - line);
        bool ret;

        if (data->header_seen) {
                ret = parse_data_line(data, line_copy);
        } else {
                data->header_seen = true;
                ret = parse_header_line(data, line_copy);
        }

        vr_free(line_copy);

        return ret;
}


/**
 * Parse the input
 *
 * If there is a parse failure, print a description of the problem and
 * then return NULL
 */
struct vr_vbo *
vr_vbo_parse(const char *text, size_t text_length)
{
        const char *text_end = text + text_length;
        struct vbo_data data = {
                .line_num = 1,
                .vbo = vr_calloc(sizeof (struct vr_vbo)),
                .raw_data = VR_BUFFER_STATIC_INIT
        };

        vr_list_init(&data.vbo->attribs);

        const char *line = text;
        while (line < text_end) {
                if (!parse_line(&data, line, text_end)) {
                        vr_vbo_free(data.vbo);
                        data.vbo = NULL;
                        break;
                }

                data.line_num++;

                const char *line_end =
                        memchr(line, '\n', text_end - line);
                if (line_end)
                        line = line_end + 1;
                else
                        break;
        }

        if (data.vbo)
                data.vbo->raw_data = data.raw_data.data;
        else
                vr_buffer_destroy(&data.raw_data);

        return data.vbo;
}

void
vr_vbo_free(struct vr_vbo *vbo)
{
        struct vr_vbo_attrib *attrib, *tmp;

        vr_list_for_each_safe(attrib, tmp, &vbo->attribs, link)
                vr_free(attrib);

        vr_free(vbo->raw_data);
        vr_free(vbo);
}
