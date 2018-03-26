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
 *   0/double/vec3      2/uint/uint     3/int/int       4/int/int
 *   0.0 0.0 0.0        10              0               0       # comment
 *   0.0 1.0 0.0         5              1               1
 *   1.0 1.0 0.0         0              0               1
 *   \endverbatim
 *
 * The format consists of a row of column headers followed by any
 * number of rows of data. Each column header has the form
 * "ATTRLOC/GL_TYPE/GLSL_TYPE", where ATTRLOC is the location of the
 * vertex attribute to be bound to this column, ARRAY_INDEX is the
 * index, GL_TYPE is the GL type of data that follows ("half",
 * "float", "double", "byte", "ubyte", "short", "ushort", "int" or
 * "uint"), GLSL_TYPE is the GLSL type of the data ("int", "uint",
 * "float", "double", "ivec"*, "uvec"*, "vec"*, "dvec"*, "mat"*,
 * "dmat"*).
 *
 * The data follows the column headers in space-separated form.  "#"
 * can be used for comments, as in shell scripts.
 *
 * To process textual vertex data, call the function
 * vr_vbo_from_text(), passing the int identifying the linked
 * program, and the string containing the vertex data.  The return
 * value is the number of rows of vertex data found.
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

/**
 * Convert a type name string to a GLenum.
 */
static bool
decode_type(const char *type,
            enum vr_vbo_type *gl_type,
            size_t *gl_type_size,
            enum vr_vbo_type *glsl_type)
{
        assert(type);
        assert(gl_type);
        assert(gl_type_size);

        static struct type_table_entry {
                const char *type; /* NULL means end of table */
                enum vr_vbo_type gl_type;
                size_t gl_type_size;
                enum vr_vbo_type glsl_type;
        } const type_table[] = {
                { "byte", VR_VBO_TYPE_BYTE, 1, VR_VBO_TYPE_INT },
                { "ubyte", VR_VBO_TYPE_UNSIGNED_BYTE, 1,
                  VR_VBO_TYPE_UNSIGNED_INT },
                { "short", VR_VBO_TYPE_SHORT, 2, VR_VBO_TYPE_INT },
                { "ushort", VR_VBO_TYPE_UNSIGNED_SHORT, 2,
                  VR_VBO_TYPE_UNSIGNED_INT },
                { "int", VR_VBO_TYPE_INT, 4, VR_VBO_TYPE_INT },
                { "uint", VR_VBO_TYPE_UNSIGNED_INT, 4,
                  VR_VBO_TYPE_UNSIGNED_INT },
                { "half", VR_VBO_TYPE_HALF_FLOAT, 2, VR_VBO_TYPE_FLOAT },
                { "float", VR_VBO_TYPE_FLOAT, 4, VR_VBO_TYPE_FLOAT },
                { "double", VR_VBO_TYPE_DOUBLE, 8, VR_VBO_TYPE_DOUBLE },
                { NULL, 0, 0, 0 },
        };


        for (int i = 0; type_table[i].type; ++i) {
                if (0 == strcmp(type, type_table[i].type)) {
                        *gl_type = type_table[i].gl_type;
                        *gl_type_size = type_table[i].gl_type_size;
                        if (glsl_type)
                                *glsl_type = type_table[i].glsl_type;
                        return true;
                }
        }

        return false;
}


/**
 * Convert a GLSL type name string to its basic GLenum type.
 */
static bool
decode_glsl_type(const char *type,
                 enum vr_vbo_type *glsl_type,
                 size_t *rows,
                 size_t *cols,
                 char **endptr)
{
        assert(glsl_type);
        assert(rows);
        assert(cols);
        assert(endptr);

        static struct type_table_entry {
                const char *type; /* NULL means end of table */
                enum vr_vbo_type glsl_type;
        } const type_table[] = {
                { "int",     VR_VBO_TYPE_INT            },
                { "uint",    VR_VBO_TYPE_UNSIGNED_INT   },
                { "float",   VR_VBO_TYPE_FLOAT          },
                { "double",  VR_VBO_TYPE_DOUBLE         },
                { "ivec",    VR_VBO_TYPE_INT            },
                { "uvec",    VR_VBO_TYPE_UNSIGNED_INT   },
                { "vec",     VR_VBO_TYPE_FLOAT          },
                { "dvec",    VR_VBO_TYPE_DOUBLE         },
                { "mat",     VR_VBO_TYPE_FLOAT          },
                { "dmat",    VR_VBO_TYPE_DOUBLE         },
                { NULL,      0                          }
        };


        for (int i = 0; type_table[i].type; ++i) {
                const size_t type_len = strlen(type_table[i].type);
                if (0 == strncmp(type, type_table[i].type, type_len)) {
                        *endptr = (char *) &type[type_len];

                        /* In case of vectors or matrices, let's
                         * calculate rows and columns.
                         */
                        if (i > 3) {
                                if (!isdigit(**endptr))
                                        goto cleanup;
                                *rows = **endptr - '0';
                                ++*endptr;

                                /* In case of matrices, let's
                                 * calculate the rows.
                                 */
                                if (i > 7) {
                                        *cols = *rows;
                                        if (**endptr == 'x') {
                                                if (!isdigit(*(++*endptr)))
                                                        goto cleanup;
                                                *rows = **endptr - '0';
                                                ++*endptr;
                                        }
                                } else {
                                        *cols = 1;
                                }
                        } else {
                                *rows = 1;
                                *cols = 1;
                        }
                        *glsl_type = type_table[i].glsl_type;
                        return true;
                }
        }

cleanup:
        *glsl_type = 0;
        *endptr = (char *) type;
        return false;
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

static void
header_error(const char *text)
{
        vr_error_message("Column headers must be in the form"
                         " location/type/dimensions/.\n"
                         "Got: %s",
                         text);
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
        char *type_str = NULL;
        bool ret = true;

        /* Split the column header into location/type/dimensions
         * fields.
         */
        const char *first_slash = strchr(text, '/');
        if (first_slash == NULL) {
                header_error(text);
                ret = false;
                goto out;
        }

        size_t name_size = first_slash - text;
        name = vr_strndup(text, name_size);

        const char *second_slash = strchr(first_slash + 1, '/');
        if (second_slash == NULL) {
                header_error(text);
                ret = false;
                goto out;
        }

        char *endptr;
        if (!decode_glsl_type(second_slash + 1,
                              &attrib->glsl_data_type,
                              &attrib->rows, &attrib->cols,
                              &endptr)) {
                vr_error_message("Unrecognized GLSL type: %s",
                                 second_slash + 1);
                ret = false;
                goto out;
        }

        type_str = vr_strndup(first_slash + 1, second_slash - first_slash - 1);

        if (!decode_type(type_str,
                         &attrib->data_type,
                         &attrib->data_type_size,
                         &attrib->glsl_data_type)) {
                vr_error_message("Unrecognized GL type: %s", type_str);
                ret = false;
                goto out;
        }

        if (*endptr != '\0') {
                header_error(text);
                ret = false;
                goto out;
        }

        if (!get_attrib_location(name, &attrib->location)) {
                vr_error_message("Unexpected vbo column name.  Got: %s",
                                 name);
                ret = false;
                goto out;
        }

        if (attrib->rows < 1 || attrib->rows > 4) {
                vr_error_message("Rows must be between 1 and 4.  Got: %lu",
                                 (unsigned long) attrib->rows);
                ret = false;
                goto out;
        }

        if (attrib->cols < 1 || attrib->cols > 4) {
                vr_error_message("Columns must be between 1 and 4.  Got: %lu",
                                 (unsigned long) attrib->cols);
                ret = false;
                goto out;
        }

out:
        vr_free(name);
        vr_free(type_str);

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
parse_datum(enum vr_vbo_type type,
            const char **text,
            void *data)
{
        char *endptr;
        errno = 0;
        switch (type) {
        case VR_VBO_TYPE_HALF_FLOAT: {
                unsigned short value = vr_hex_strtohf(*text, &endptr);
                if (errno == ERANGE) {
                        vr_error_message("Could not parse as half float");
                        return false;
                }
                *((uint16_t *) data) = value;
                break;
        }
        case VR_VBO_TYPE_FLOAT: {
                float value = vr_hex_strtof(*text, &endptr);
                if (errno == ERANGE) {
                        vr_error_message("Could not parse as float");
                        return false;
                }
                *((float *) data) = value;
                break;
        }
        case VR_VBO_TYPE_DOUBLE: {
                double value = vr_hex_strtod(*text, &endptr);
                if (errno == ERANGE) {
                        vr_error_message("Could not parse as double");
                        return false;
                }
                *((double *) data) = value;
                break;
        }
        case VR_VBO_TYPE_BYTE: {
                long value = vr_hex_strtol(*text, &endptr);
                if (errno == ERANGE || value < INT8_MIN || value > INT8_MAX) {
                        vr_error_message("Could not parse as signed byte");
                        return false;
                }
                *((int8_t *) data) = (int8_t) value;
                break;
        }
        case VR_VBO_TYPE_UNSIGNED_BYTE: {
                unsigned long value = strtoul(*text, &endptr, 0);
                if (errno == ERANGE || value > UINT8_MAX) {
                        vr_error_message("Could not parse as unsigned byte");
                        return false;
                }
                *((uint8_t *) data) = (uint8_t) value;
                break;
        }
        case VR_VBO_TYPE_SHORT: {
                long value = vr_hex_strtol(*text, &endptr);
                if (errno == ERANGE ||
                    value < INT16_MIN || value > INT16_MAX) {
                        vr_error_message("Could not parse as signed short");
                        return false;
                }
                *((int16_t *) data) = (uint16_t) value;
                break;
        }
        case VR_VBO_TYPE_UNSIGNED_SHORT: {
                unsigned long value = strtoul(*text, &endptr, 0);
                if (errno == ERANGE || value > UINT16_MAX) {
                        vr_error_message("Could not parse as unsigned short");
                        return false;
                }
                *((uint16_t *) data) = (uint16_t) value;
                break;
        }
        case VR_VBO_TYPE_INT: {
                long value = vr_hex_strtol(*text, &endptr);
                if (errno == ERANGE || value < INT32_MIN || value > INT32_MAX) {
                        vr_error_message("Could not parse as "
                                         "signed integer");
                        return false;
                }
                *((int32_t *) data) = (int32_t) value;
                break;
        }
        case VR_VBO_TYPE_UNSIGNED_INT: {
                unsigned long value = strtoul(*text, &endptr, 0);
                if (errno == ERANGE || value >= UINT32_MAX) {
                        vr_error_message("Could not parse as "
                                         "unsigned integer");
                        return false;
                }
                *((uint32_t *) data) = (uint32_t) value;
                break;
        }
        default:
                assert(!"Unexpected data type");
                endptr = NULL;
                break;
        }
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

                attrib->offset = vbo->stride;
                vbo->stride += attrib->rows * attrib->data_type_size;
                pos = column_header_end + 1;
        }

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
        uint8_t *data_ptr = data->raw_data.data + old_length;

        const char *line_ptr = line;
        const struct vr_vbo_attrib *attrib;

        vr_list_for_each(attrib, &vbo->attribs, link) {
                for (size_t j = 0; j < attrib->rows; ++j) {
                        if (!parse_datum(attrib->data_type,
                                         &line_ptr,
                                         data_ptr)) {
                                vr_error_message("At line %u of [vertex data] "
                                                 "section. "
                                                 "Offending text: %s",
                                                 data->line_num,
                                                 line_ptr);
                                return false;
                        }
                        data_ptr += attrib->data_type_size;
                }
        }

        ++vbo->num_rows;

        return true;
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
        while (line < text_end && isspace(*line))
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
