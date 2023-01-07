#!/usr/bin/env python

# Copyright (C) 2018 Intel Corporation
# Copyright 2023 Neil Roberts

# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

from __future__ import (
    absolute_import, division, print_function, unicode_literals
)

# This script is used to generate format_table.rs from vulkan.h. It is
# not run automatically as part of the build process but if need be it
# can be used to update the file as follows:
#
# ./make-formats.py < /usr/include/vulkan/vulkan.h > format_table.rs

import re
import sys
from mako.template import Template

FORMAT_RE = re.compile(r'\bVK_FORMAT_([A-Z0-9_]+)\s*=\s*((?:0x)?[a-fA-F0-9]+)')
SKIP_RE = re.compile(r'(?:_BLOCK(?:_IMG)?|_KHR|^UNDEFINED|'
                     r'^RANGE_SIZE|^MAX_ENUM|_RANGE)$')
COMPONENT_RE = re.compile('([A-Z]+)([0-9]+)')
COMPONENTS_RE = re.compile('(?:[A-Z][0-9]+)+$')
STUB_RE = re.compile('X([0-9]+)$')
PACK_RE = re.compile('PACK([0-9]+)$')
MODE_RE = re.compile('(?:[US](?:NORM|SCALED|INT|FLOAT)|SRGB)$')

TEMPLATE="""\
// Automatically generated by make-formats.py

static FORMATS: [Format; ${len(formats)}] = [
% for format in formats:
    Format {
        vk_format: vk::VK_FORMAT_${format['name']},
        name: "${format['name']}",
        % if format['packed_size'] is None:
        packed_size: None,
        % else:
        packed_size: Some(unsafe {
            NonZeroUsize::new_unchecked(${format['packed_size']})
        }),
        % endif
        n_parts: ${len(format['components'])},
        parts: [
            % for letter, size, mode in format['components']:
            Part {
                bits: ${size},
                component: Component::${letter},
                mode: Mode::${mode},
            },
            % endfor
            % for _ in range(len(format['components']), 4):
            // stub to fill the array
            Part { bits: 0, component: Component::R, mode: Mode::UNORM },
            % endfor
        ]
    },
% endfor
];"""


def get_format_names(data):
    in_enum = False

    for line in data:
        if line.startswith('typedef enum VkFormat '):
            in_enum = True
        elif line.startswith('}'):
            in_enum = False
        if not in_enum:
            continue

        md = FORMAT_RE.search(line)
        if md is None:
            continue
        name = md.group(1)
        if SKIP_RE.search(name):
            continue
        yield name, int(md.group(2), base=0)


def get_formats(data):
    for name, value in sorted(set(get_format_names(data))):
        parts = name.split('_')

        components, packed_size = get_components(parts)

        if components is None:
            continue

        yield {'name': name,
               'value': value,
               'packed_size': packed_size,
               'components': components}


def get_components(parts):
    packed_size = None
    components = []

    i = 0
    while i < len(parts):
        md = STUB_RE.match(parts[i])
        if md:
            components.append(('X', int(md.group(1)), 'UNORM'))
            i += 1
            continue

        md = COMPONENTS_RE.match(parts[i])
        if md:
            if i + 2 > len(parts):
                return None, None
            mode_md = MODE_RE.match(parts[i + 1])
            if mode_md is None:
                return None, None

            comps = [(md.group(1), int(md.group(2)), parts[i + 1])
                     for md in COMPONENT_RE.finditer(parts[i])]

            for letter, size, mode in comps:
                if letter not in "RGBADSX":
                    return None, None

            components.extend(comps)
            i += 2
            continue

        md = PACK_RE.match(parts[i])
        if md:
            packed_size = int(md.group(1))
            i += 1
            continue

        return None, None

    return components, packed_size


def main():
    template = Template(TEMPLATE)
    print(template.render(formats = list(get_formats(sys.stdin))))


if __name__ == '__main__':
    main()
