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

#include "vr-format-private.h"
#include "vr-util.h"

#include <string.h>
#include <assert.h>

#include "vr-format-table.h"
#include "vr-small-float.h"

char *
vr_format_get_name(const struct vr_format *format)
{
        return vr_strdup(format->name_start);
}

const struct vr_format *
vr_format_lookup_by_name(const char *name)
{
        for (int i = 0; i < VR_N_ELEMENTS(formats); i++) {
                if (!vr_strcasecmp(formats[i].name_start, name))
                        return formats + i;
        }

        return NULL;
}

const struct vr_format *
vr_format_lookup_by_vk_format(VkFormat vk_format)
{
        for (int i = 0; i < VR_N_ELEMENTS(formats); i++) {
                if (formats[i].vk_format == vk_format)
                        return formats + i;
        }

        return NULL;
}

const struct vr_format *
vr_format_lookup_by_details(int bit_size,
                            enum vr_format_mode mode,
                            int n_components)
{
        static const enum vr_format_component comp_order[] = {
                VR_FORMAT_COMPONENT_R,
                VR_FORMAT_COMPONENT_G,
                VR_FORMAT_COMPONENT_B,
                VR_FORMAT_COMPONENT_A,
        };

        for (int i = 0; i < VR_N_ELEMENTS(formats); i++) {
                if (formats[i].n_parts != n_components ||
                    formats[i].packed_size != 0)
                        continue;

                for (int j = 0; j < n_components; j++) {
                        if (formats[i].parts[j].bits != bit_size ||
                            formats[i].parts[j].component != comp_order[j] ||
                            formats[i].parts[j].mode != mode)
                                goto bad_format;
                }

                return formats + i;

        bad_format:
                continue;
        }

        return NULL;
}

int
vr_format_get_size(const struct vr_format *format)
{
        if (format->packed_size)
                return format->packed_size / 8;

        int total_size = 0;

        for (int i = 0; i < format->n_parts; i++)
                total_size += format->parts[i].bits;

        return total_size / 8;
}

static int32_t
sign_extend(uint32_t part, int bits)
{
        if (part & (1 << (bits - 1)))
                return (UINT32_MAX << bits) | part;
        else
                return part;
}

static double
load_packed_part(uint32_t part,
                 int bits,
                 enum vr_format_mode mode)
{
        assert(bits < 32);

        switch (mode) {
        case VR_FORMAT_MODE_SRGB:
        case VR_FORMAT_MODE_UNORM:
                return part / (double) ((1 << bits) - 1);
        case VR_FORMAT_MODE_SNORM:
                return (sign_extend(part, bits) /
                        (double) ((1 << (bits - 1)) - 1));
        case VR_FORMAT_MODE_UINT:
        case VR_FORMAT_MODE_USCALED:
                return part;
        case VR_FORMAT_MODE_SSCALED:
        case VR_FORMAT_MODE_SINT:
                return sign_extend(part, bits);
        case VR_FORMAT_MODE_UFLOAT:
                switch (bits) {
                case 10:
                        return vr_small_float_load_unsigned(part, 5, 5);
                case 11:
                        return vr_small_float_load_unsigned(part, 5, 6);
                default:
                        vr_fatal("unknown bit size in packed UFLOAT format");
                }
        case VR_FORMAT_MODE_SFLOAT:
                vr_fatal("Unexpected packed SFLOAT format");
        }

        vr_fatal("Unknown packed format");
}

static void
load_packed_parts(const struct vr_format *format,
                  const uint8_t *fb,
                  double *parts)
{
        uint64_t packed_parts;

        switch (format->packed_size) {
        case 8:
                packed_parts = *fb;
                break;
        case 16:
                packed_parts = *(uint16_t *) fb;
                break;
        case 32:
                packed_parts = *(uint32_t *) fb;
                break;
        default:
                vr_fatal("Unknown packed bit size: %zu", format->packed_size);
        }

        for (int i = format->n_parts - 1; i >= 0; i--) {
                int bits = format->parts[i].bits;
                uint32_t part = packed_parts & ((1 << bits) - 1);

                parts[i] = load_packed_part(part, bits, format->parts[i].mode);
                packed_parts >>= bits;
        }
}

static double
load_part(int bits,
          const uint8_t *fb,
          enum vr_format_mode mode)
{
        switch (mode) {
        case VR_FORMAT_MODE_SRGB:
        case VR_FORMAT_MODE_UNORM:
                switch (bits) {
                case 8:
                        return *fb / (double) UINT8_MAX;
                case 16:
                        return (*(uint16_t *) fb) / (double) UINT16_MAX;
                case 32:
                        return (*(uint32_t *) fb) / (double) UINT32_MAX;
                case 64:
                        return (*(uint64_t *) fb) / (double) UINT64_MAX;
                }
                break;
        case VR_FORMAT_MODE_SNORM:
                switch (bits) {
                case 8:
                        return (*(int8_t *) fb) / (double) INT8_MAX;
                case 16:
                        return (*(int16_t *) fb) / (double) INT16_MAX;
                case 32:
                        return (*(int32_t *) fb) / (double) INT32_MAX;
                case 64:
                        return (*(int64_t *) fb) / (double) INT64_MAX;
                }
                break;
        case VR_FORMAT_MODE_UINT:
        case VR_FORMAT_MODE_USCALED:
                switch (bits) {
                case 8:
                        return *fb;
                case 16:
                        return *(uint16_t *) fb;
                case 32:
                        return *(uint32_t *) fb;
                case 64:
                        return *(uint64_t *) fb;
                }
                break;
        case VR_FORMAT_MODE_SINT:
        case VR_FORMAT_MODE_SSCALED:
                switch (bits) {
                case 8:
                        return *(int8_t *) fb;
                case 16:
                        return *(int16_t *) fb;
                case 32:
                        return *(int32_t *) fb;
                case 64:
                        return *(int64_t *) fb;
                }
                break;
        case VR_FORMAT_MODE_UFLOAT:
                break;
        case VR_FORMAT_MODE_SFLOAT:
                switch (bits) {
                case 16:
                        return vr_small_float_load_signed(*(uint16_t *) fb,
                                                          5, 10);
                case 32:
                        return *(float *) fb;
                case 64:
                        return *(double *) fb;
                }
                break;
        }

        vr_fatal("Unknown format bit size combination");
}

void
vr_format_load_pixel(const struct vr_format *format,
                     const void *source,
                     double *pixel)
{
        const uint8_t *p = source;

        for (int i = 0; i < 3; i++)
                pixel[i] = 0.0;
        /* Alpha component defaults to 1.0 if not contained in the format */
        pixel[3] = 1.0f;

        double parts[4];

        if (format->packed_size) {
                load_packed_parts(format, p, parts);
        } else {
                for (int i = 0; i < format->n_parts; i++) {
                        int bits = format->parts[i].bits;
                        parts[i] = load_part(bits, p, format->parts[i].mode);
                        p += bits / 8;
                }
        }

        for (int i = 0; i < format->n_parts; i++) {
                switch (format->parts[i].component) {
                case VR_FORMAT_COMPONENT_R:
                        pixel[0] = parts[i];
                        break;
                case VR_FORMAT_COMPONENT_G:
                        pixel[1] = parts[i];
                        break;
                case VR_FORMAT_COMPONENT_B:
                        pixel[2] = parts[i];
                        break;
                case VR_FORMAT_COMPONENT_A:
                        pixel[3] = parts[i];
                        break;
                case VR_FORMAT_COMPONENT_D:
                case VR_FORMAT_COMPONENT_S:
                case VR_FORMAT_COMPONENT_X:
                        break;
                }
        }
}
