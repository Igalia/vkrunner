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
