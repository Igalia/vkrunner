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

#include "vr-format.h"
#include "vr-util.h"

#include <string.h>
#include <assert.h>

#include "vr-format-table.h"

const struct vr_format *
vr_format_lookup_by_name(const char *name)
{
        for (int i = 0; i < VR_N_ELEMENTS(formats); i++) {
                if (!strcasecmp(formats[i].name, name))
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
        for (int i = 0; i < VR_N_ELEMENTS(formats); i++) {
                if (formats[i].n_components != n_components ||
                    formats[i].mode != mode ||
                    formats[i].packed_size != 0 ||
                    formats[i].swizzle != VR_FORMAT_SWIZZLE_RGBA)
                        continue;

                for (int j = 0; j < n_components; j++) {
                        if (formats[i].components[j].bits != bit_size)
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

        for (int i = 0; i < format->n_components; i++)
                total_size += format->components[i].bits;

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
                vr_fatal("FIXME: load from packed UFLOAT format");
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
                vr_fatal("Unknown packed bit size: %i", format->packed_size);
        }

        for (int i = format->n_components - 1; i >= 0; i--) {
                int bits = format->components[i].bits;
                uint32_t part = packed_parts & ((1 << bits) - 1);

                parts[i] = load_packed_part(part, bits, format->mode);
                packed_parts >>= bits;
        }
}

static double
load_part(const struct vr_format *format,
          int bits,
          const uint8_t *fb)
{
        switch (format->mode) {
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
                        vr_fatal("FIXME: load pixel from half-float format");
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
                     const uint8_t *p,
                     double *pixel)
{
        double parts[4] = { 0.0f };

        /* Alpha component defaults to 1.0 if not contained in the format */
        switch (format->swizzle) {
        case VR_FORMAT_SWIZZLE_BGRA:
        case VR_FORMAT_SWIZZLE_RGBA:
                parts[3] = 1.0f;
                break;
        case VR_FORMAT_SWIZZLE_ARGB:
        case VR_FORMAT_SWIZZLE_ABGR:
                parts[0] = 1.0f;
                break;
        }

        if (format->packed_size) {
                load_packed_parts(format, p, parts);
        } else {
                for (int i = 0; i < format->n_components; i++) {
                        int bits = format->components[i].bits;
                        parts[i] = load_part(format, bits, p);
                        p += bits / 8;
                }
        }

        switch (format->swizzle) {
        case VR_FORMAT_SWIZZLE_RGBA:
                memcpy(pixel, parts, sizeof parts);
                break;
        case VR_FORMAT_SWIZZLE_ARGB:
                memcpy(pixel, parts + 1, sizeof (double) * 3);
                pixel[2] = parts[0];
                break;
        case VR_FORMAT_SWIZZLE_BGRA:
                pixel[0] = parts[2];
                pixel[1] = parts[1];
                pixel[2] = parts[0];
                pixel[3] = parts[3];
                break;
        case VR_FORMAT_SWIZZLE_ABGR:
                pixel[0] = parts[3];
                pixel[1] = parts[2];
                pixel[2] = parts[1];
                pixel[3] = parts[0];
                break;
        }
}
