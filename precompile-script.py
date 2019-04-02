#!/usr/bin/env python

# Copyright (C) 2018 Intel Corporation

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

import re
import tempfile
import sys
import subprocess
import argparse
import os
import struct

TARGET_ENV = "vulkan1.0"

SECTION_RE = re.compile(r'^\[([^]]+)\]\s*$')
VERSION_RE = re.compile(r'^(\s*vulkan\s*\d+\.\d+)(\.\d+\s*)$')
TRIM_RE = re.compile(r'\s+')

STAGE_MAP = {
    'vertex shader': 'vert',
    'tessellation control shader': 'tesc',
    'tessellation evaluation shader': 'tese',
    'geometry shader': 'geom',
    'fragment shader': 'frag',
    'compute shader': 'comp',
}

class Converter:
    def __init__(self, type, stage, fout, binary, version):
        self._type = type
        self._stage = stage
        self._fout = fout
        self._tempfile = tempfile.NamedTemporaryFile('w+')
        self._binary = binary
        self._version = version

    def add_line(self, line):
        self._tempfile.write(line)

    def finish(self):
        self._tempfile.flush()

        with tempfile.NamedTemporaryFile() as temp_outfile:
            if self._type == 'glsl':
                subprocess.check_call([self._binary,
                                       "-S", self._stage,
                                       "-G",
                                       "-V",
                                       "--target-env", self._version,
                                       "-o", temp_outfile.name,
                                       self._tempfile.name])
            else:
                subprocess.check_call([self._binary,
                                       "--target-env", self._version,
                                       "-o", temp_outfile.name,
                                       self._tempfile.name])

            data = temp_outfile.read()

        self._tempfile.close()

        if len(data) < 4 or len(data) % 4 != 0:
            print("Resulting binary SPIR-V file has an invalid size",
                  file=sys.stderr)
            sys.exit(1)

        magic_header = struct.unpack(">I", data[0:4])[0]

        if magic_header == 0x07230203:
            byte_order = ">I"
        elif magic_header == 0x03022307:
            byte_order = "<I"
        else:
            print("Resulting binary SPIR-V has an invalid magic number",
                  file=sys.stderr)
            sys.exit(1)

        line_pos = 0
        for offset in range(0, len(data), 4):
            value = struct.unpack(byte_order, data[offset:offset+4])[0]
            hex_str = "{:x}".format(value)
            if len(hex_str) + line_pos + 1 > 80:
                line_pos = 0
                print(file=self._fout)
            elif line_pos > 0:
                print(' ', file=self._fout, end='')
                line_pos += 1
            print(hex_str, file=self._fout, end='')
            line_pos += len(hex_str)

        if line_pos > 0:
            print(file=self._fout)
        print(file=self._fout)

def convert_stream(fin, fout, glslang, spirv_as):
    section_name = None
    converter = None
    version = TARGET_ENV

    for line in fin:
        md = SECTION_RE.match(line)
        if md:
            if converter:
                converter.finish()
                converter = None

            section_name = md.group(1)

            if section_name.endswith(' spirv'):
                stage_name = section_name[:-6]
                converter = Converter('spirv',
                                      STAGE_MAP[stage_name],
                                      fout,
                                      spirv_as,
                                      version)
                print("[{} binary]".format(stage_name), file=fout)
            elif section_name in STAGE_MAP:
                converter = Converter('glsl',
                                      STAGE_MAP[section_name],
                                      fout, glslang, version)
                print("[{} binary]".format(section_name), file=fout)
            else:
                fout.write(line)
        elif converter:
            converter.add_line(line)
        else:
            if section_name == 'require':
                vmd = VERSION_RE.match(line)
                if vmd:
                    version = vmd.group(1)
                    version = TRIM_RE.sub('', version)
            fout.write(line)

    if converter:
        converter.finish()


parser = argparse.ArgumentParser(description='Precompile VkRunner scripts.')
parser.add_argument('inputs', metavar='INPUT', type=str, nargs='+',
                    help='an input file process')
parser.add_argument('-o', dest='output', metavar='OUTPUT',
                    help='an output file or directory', required=True)

parser.add_argument('-g', dest='glslang', metavar='GLSLANG',
                    help='glslangValidator binary path', required=False, default="glslangValidator")

parser.add_argument('-s', dest='spirv_as', metavar='SPIRV_AS',
                    help='spirv-as binary path', required=False, default="spirv-as")

args = parser.parse_args()
output_is_directory = len(args.inputs) >= 2 or os.path.isdir(args.output)

if output_is_directory:
    try:
        os.mkdir(args.output)
    except OSError:
        pass

for input in args.inputs:
    if output_is_directory:
        output = os.path.join(args.output, os.path.basename(input))
    else:
        output = args.output

    with open(input, 'r') as fin:
        with open(output, 'w') as fout:
            convert_stream(fin, fout, args.glslang, args.spirv_as)
