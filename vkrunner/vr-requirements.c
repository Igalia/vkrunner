/*
 * vkrunner
 *
 * Copyright (C) 2019 Intel Corporation
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

#include "vr-requirements.h"

#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "vr-util.h"
#include "vr-feature.h"
#include "vr-vk.h"
#include "vr-list.h"
#include "vr-buffer.h"

struct base_structure {
        VkStructureType type;
        struct base_structure *next;
};

struct full_structure {
        const struct vr_feature_extension *extension;
        struct vr_list link;
        struct base_structure base;
};

struct vr_requirements {
        struct vr_buffer extensions;
        struct vr_list structures;
        VkPhysicalDeviceFeatures features;
};

struct vr_requirements *
vr_requirements_new(void)
{
        struct vr_requirements *reqs = vr_calloc(sizeof *reqs);

        vr_buffer_init(&reqs->extensions);
        vr_list_init(&reqs->structures);

        return reqs;
}

const char* const*
vr_requirements_get_extensions(const struct vr_requirements *reqs)
{
        return (const char * const *) reqs->extensions.data;
}

size_t
vr_requirements_get_n_extensions(const struct vr_requirements *reqs)
{
        return reqs->extensions.length / sizeof (const char *);
}

static const void *
get_structures_from_list(const struct vr_list *list)
{
        if (vr_list_empty(list))
                return NULL;

        struct full_structure *structure =
                vr_container_of(list->next,
                                struct full_structure,
                                link);

        return &structure->base;
}

const void *
vr_requirements_get_structures(const struct vr_requirements *reqs)
{
        return get_structures_from_list(&reqs->structures);
}

const VkPhysicalDeviceFeatures *
vr_requirements_get_base_features(const struct vr_requirements *reqs)
{
        return &reqs->features;
}

static void
add_extension_name(struct vr_requirements *reqs,
                   const char *name)
{
        size_t n_exts = vr_requirements_get_n_extensions(reqs);
        const char *const *exts = vr_requirements_get_extensions(reqs);

        for (size_t i = 0; i < n_exts; i++) {
                if (!strcmp(exts[i], name))
                        return;
        }

        char *name_copy = vr_strdup(name);
        vr_buffer_append(&reqs->extensions, &name_copy, sizeof name_copy);
}

static struct full_structure *
add_structure_type_to_list(struct vr_list *list,
                           VkStructureType struct_type,
                           size_t struct_size)
{
        struct full_structure *structure =
                vr_calloc(offsetof(struct full_structure, base) + struct_size);

        structure->base.type = struct_type;
        structure->base.next = (void *) get_structures_from_list(list);

        vr_list_insert(list, &structure->link);

        return structure;
}

static struct full_structure *
add_structure_to_list(struct vr_list *list,
                      const struct vr_feature_extension *extension)
{
        struct full_structure *structure =
                add_structure_type_to_list(list,
                                           extension->struct_type,
                                           extension->struct_size);

        structure->extension = extension;

        return structure;
}

static bool
find_feature(const char *name,
             const struct vr_feature_extension **extension_out,
             const struct vr_feature_offset **offset_out)
{
        for (const struct vr_feature_extension *ext = vr_feature_extensions;
             ext->struct_size > 0;
             ext++) {
                for (const struct vr_feature_offset *offset = ext->offsets;
                     offset->name;
                     offset++) {
                        if (!strcmp(offset->name, name)) {
                                *extension_out = ext;
                                *offset_out = offset;
                                return true;
                        }
                }
        }

        return false;
}

static uint8_t *
get_structure(struct vr_requirements *reqs,
              const struct vr_feature_extension *extension)
{
        struct full_structure *structure;

        vr_list_for_each(structure, &reqs->structures, link) {
                if (structure->extension == extension)
                        return (uint8_t *) &structure->base;
        }

        structure = add_structure_to_list(&reqs->structures, extension);

        return (uint8_t *) &structure->base;
}

void
vr_requirements_add(struct vr_requirements *reqs,
                    const char *name)
{
        const struct vr_feature_extension *extension;
        const struct vr_feature_offset *offset;
        bool is_feature = find_feature(name, &extension, &offset);

        if (is_feature) {
                uint8_t *structure;

                if (extension->name) {
                        add_extension_name(reqs, extension->name);
                        structure = get_structure(reqs, extension);
                } else {
                        structure = (uint8_t *) &reqs->features;
                }

                *(VkBool32 *) (structure + offset->offset) = true;
        } else {
                add_extension_name(reqs, name);
        }
}

bool
vr_requirements_equal(const struct vr_requirements *reqs_a,
                      const struct vr_requirements *reqs_b)
{
        if (memcmp(&reqs_a->features,
                   &reqs_b->features,
                   sizeof reqs_a->features))
                return false;

        size_t n_exts = vr_requirements_get_n_extensions(reqs_a);

        if (n_exts != vr_requirements_get_n_extensions(reqs_b))
                return false;

        const char *const *exts_a = vr_requirements_get_extensions(reqs_a);
        const char *const *exts_b = vr_requirements_get_extensions(reqs_b);

        for (size_t i = 0; i < n_exts; i++) {
                if (strcmp(exts_a[i], exts_b[i]))
                        return false;
        }

        const struct full_structure *struct_a, *struct_b;

        struct_b = vr_container_of(reqs_b->structures.next,
                                   struct full_structure,
                                   link);

        vr_list_for_each(struct_a, &reqs_a->structures, link) {
                /* If we’ve reached the end of the b reqs then the
                 * list is shorter than reqs_a.
                 */
                if (&struct_b->link == &reqs_b->structures)
                        return false;

                if (struct_a->extension != struct_b->extension)
                        return false;

                assert(struct_a->base.type == struct_b->base.type);

                if (memcmp((uint8_t *) &struct_a->base +
                           sizeof (struct base_structure),
                           (uint8_t *) &struct_b->base +
                           sizeof (struct base_structure),
                           struct_a->extension->struct_size -
                           sizeof (struct base_structure)))
                        return false;

                struct_b = vr_container_of(struct_b->link.next,
                                           struct full_structure,
                                           link);
        }

        /* If struct_b is not at the end of the list then it is too long */
        if (&struct_b->link != &reqs_b->structures)
                return false;

        return true;
}

static void
copy_structures(const struct vr_requirements *reqs,
                struct vr_list *list)
{
        vr_list_init(list);

        struct full_structure *struct_a;

        vr_list_for_each_reverse(struct_a, &reqs->structures, link) {
                struct full_structure *struct_b =
                        add_structure_to_list(list, struct_a->extension);

                memcpy((uint8_t *) &struct_a->base +
                       sizeof (struct base_structure),
                       (uint8_t *) &struct_b->base +
                       sizeof (struct base_structure),
                       struct_a->extension->struct_size -
                       sizeof (struct base_structure));
        }
}

struct vr_requirements *
vr_requirements_copy(const struct vr_requirements *reqs)
{
        struct vr_requirements *reqs_copy = vr_calloc(sizeof *reqs_copy);

        vr_buffer_init(&reqs_copy->extensions);

        size_t n_exts = vr_requirements_get_n_extensions(reqs);
        const char *const *exts = vr_requirements_get_extensions(reqs);

        for (size_t i = 0; i < n_exts; i++) {
                char *ext_copy = vr_strdup(exts[i]);
                vr_buffer_append(&reqs_copy->extensions,
                                 &ext_copy,
                                 sizeof ext_copy);
        }

        copy_structures(reqs, &reqs_copy->structures);

        reqs_copy->features = reqs->features;

        assert(vr_requirements_equal(reqs, reqs_copy));

        return reqs_copy;
}

static bool
get_feature_value(const void *structure,
                  const struct vr_feature_offset *offset)
{
        const uint8_t *buf = (const uint8_t *) structure;
        return *(const VkBool32 *) (buf + offset->offset);
}

static bool
check_features(const struct vr_feature_offset *offsets,
               const void *requested,
               const void *actual)
{
        for (int i = 0; offsets[i].name; i++) {
                if (get_feature_value(requested, offsets + i) &&
                    !get_feature_value(actual, offsets + i))
                        return false;
        }

        return true;
}

static bool
find_extension(uint32_t property_count,
               const VkExtensionProperties *props,
               const char *extension)
{
        for (uint32_t i = 0; i < property_count; i++) {
                if (!strcmp(extension, props[i].extensionName))
                        return true;
        }

        return false;
}

static bool
check_extensions(const struct vr_requirements *reqs,
                 struct vr_vk *vkfn,
                 VkPhysicalDevice device)
{
        size_t n_exts = vr_requirements_get_n_extensions(reqs);

        if (n_exts <= 0)
                return true;

        const char *const *exts = vr_requirements_get_extensions(reqs);

        VkResult res;
        uint32_t property_count;

        res = vkfn->vkEnumerateDeviceExtensionProperties(device,
                                                         NULL, /* layerName */
                                                         &property_count,
                                                         NULL /* properties */);
        if (res != VK_SUCCESS)
                return false;

        VkExtensionProperties *props = vr_alloc(property_count * sizeof *props);
        bool ret = true;

        res = vkfn->vkEnumerateDeviceExtensionProperties(device,
                                                         NULL, /* layerName */
                                                         &property_count,
                                                         props);
        if (res == VK_SUCCESS) {
                for (size_t i = 0; i < n_exts; i++) {
                        if (!find_extension(property_count, props, exts[i])) {
                                ret = false;
                                break;
                        }
                }
        } else {
                ret = false;
        }

        vr_free(props);

        return ret;
}

static void
free_structures(struct vr_list *list)
{
        struct full_structure *structure, *tmp;

        vr_list_for_each_safe(structure, tmp, list, link) {
                vr_free(structure);
        }
}

static bool
check_structures(const struct vr_requirements *reqs,
                 struct vr_vk *vkfn,
                 VkInstance instance,
                 VkPhysicalDevice device)
{
        if (vr_list_empty(&reqs->structures))
                return true;

        const char *get_features_name = "vkGetPhysicalDeviceFeatures2KHR";
        PFN_vkGetPhysicalDeviceFeatures2 get_features =
                (void *) vkfn->vkGetInstanceProcAddr(instance,
                                                     get_features_name);

        if (get_features == NULL)
                return false;

        struct vr_list structures;
        vr_list_init(&structures);

        struct full_structure *structure;

        vr_list_for_each_reverse(structure, &reqs->structures, link) {
                add_structure_to_list(&structures, structure->extension);
        }

        add_structure_type_to_list(&structures,
                                   VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                   sizeof (VkPhysicalDeviceFeatures2));

        bool ret = true;

        get_features(device, (void *) get_structures_from_list(&structures));

        const struct full_structure *requested, *actual;

        actual = vr_container_of(structures.next->next,
                                 struct full_structure,
                                 link);

        vr_list_for_each(requested, &reqs->structures, link) {
                if (!check_features(requested->extension->offsets,
                                    &requested->base,
                                    &actual->base)) {
                        ret = false;
                        break;
                }

                actual = vr_container_of(actual->link.next,
                                         struct full_structure,
                                         link);
        }

        free_structures(&structures);

        return ret;
}

bool
vr_requirements_check(const struct vr_requirements *reqs,
                      struct vr_vk *vkfn,
                      VkInstance instance,
                      VkPhysicalDevice device)
{
        VkPhysicalDeviceFeatures features;

        vkfn->vkGetPhysicalDeviceFeatures(device, &features);

        if (!check_features(vr_feature_base_offsets,
                            &reqs->features,
                            &features))
                return false;

        if (!check_extensions(reqs, vkfn, device))
                return false;

        if (!check_structures(reqs, vkfn, instance, device))
                return false;

        return true;
}

void
vr_requirements_free(struct vr_requirements *reqs)
{
        size_t n_exts = vr_requirements_get_n_extensions(reqs);
        const char *const *exts = vr_requirements_get_extensions(reqs);

        for (size_t i = 0; i < n_exts; i++)
                vr_free((char *) exts[i]);

        vr_buffer_destroy(&reqs->extensions);

        free_structures(&reqs->structures);

        vr_free(reqs);
}
