/*
 * vkrunner
 *
 * Copyright (C) 2018 Google LLC
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

struct sampler_attribute {
        const char *name;
        union {
                VkFilter filter;
                VkSamplerMipmapMode mipmap;
                VkSamplerAddressMode address;
                VkCompareOp compare;
                VkBorderColor border;
        };
};

VkSamplerCreateInfo default_sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = NULL,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .maxAnisotropy = 1,
        .compareOp = VK_COMPARE_OP_NEVER,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
};

#define VR_SAMPLER_ATTRIBUTE(value) \
        { .name = #value, .filter = value }
struct sampler_attribute sampler_filter[] = {
        VR_SAMPLER_ATTRIBUTE(VK_FILTER_NEAREST),
        VR_SAMPLER_ATTRIBUTE(VK_FILTER_LINEAR),
        VR_SAMPLER_ATTRIBUTE(VK_FILTER_CUBIC_IMG)
};
#undef VR_SAMPLER_ATTRIBUTE

#define VR_SAMPLER_ATTRIBUTE(value) \
        { .name = #value, .mipmap = value }
struct sampler_attribute sampler_mipmap[] = {
        VR_SAMPLER_ATTRIBUTE(VK_SAMPLER_MIPMAP_MODE_NEAREST),
        VR_SAMPLER_ATTRIBUTE(VK_SAMPLER_MIPMAP_MODE_LINEAR)
};
#undef VR_SAMPLER_ATTRIBUTE

#define VR_SAMPLER_ATTRIBUTE(value) \
        { .name = #value, .address = value }
struct sampler_attribute sampler_address[] = {
        VR_SAMPLER_ATTRIBUTE(VK_SAMPLER_ADDRESS_MODE_REPEAT),
        VR_SAMPLER_ATTRIBUTE(VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT),
        VR_SAMPLER_ATTRIBUTE(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
        VR_SAMPLER_ATTRIBUTE(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER),
        VR_SAMPLER_ATTRIBUTE(VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE)
};
#undef VR_SAMPLER_ATTRIBUTE

#define VR_SAMPLER_ATTRIBUTE(value) \
        { .name = #value, .compare = value }
struct sampler_attribute sampler_compare[] = {
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_NEVER),
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_LESS),
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_EQUAL),
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_LESS_OR_EQUAL),
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_GREATER),
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_NOT_EQUAL),
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_GREATER_OR_EQUAL),
        VR_SAMPLER_ATTRIBUTE(VK_COMPARE_OP_ALWAYS)
};
#undef VR_SAMPLER_ATTRIBUTE

#define VR_SAMPLER_ATTRIBUTE(value) \
        { .name = #value, .border = value }
struct sampler_attribute sampler_border[] = {
        VR_SAMPLER_ATTRIBUTE(VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK),
        VR_SAMPLER_ATTRIBUTE(VK_BORDER_COLOR_INT_TRANSPARENT_BLACK),
        VR_SAMPLER_ATTRIBUTE(VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK),
        VR_SAMPLER_ATTRIBUTE(VK_BORDER_COLOR_INT_OPAQUE_BLACK),
        VR_SAMPLER_ATTRIBUTE(VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE),
        VR_SAMPLER_ATTRIBUTE(VK_BORDER_COLOR_INT_OPAQUE_WHITE),
};
#undef VR_SAMPLER_ATTRIBUTE

#define VR_SCRIPT_RESOURCE_ATTRIBUTE_FIND(resource, attr, ret, target)         \
        do {                                                                   \
                bool found = false;                                            \
                for (uint32_t x = 0; x < VR_N_ELEMENTS(resource##_##attr); ++x)\
                        if (looking_at(target, resource##_##attr[x].name)) {   \
                                ret = resource##_##attr[x].attr;               \
                                found = true;                                  \
                                break;                                         \
                        }                                                      \
                if (!found) {                                                  \
                        vr_error_message(data->config,                         \
                                         "%s:%i Invalid " #attr,               \
                                         data->filename,                       \
                                         data->line_num);                      \
                        return false;                                          \
                }                                                              \
        } while(0)
