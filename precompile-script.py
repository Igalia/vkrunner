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
import base64
import sys
import subprocess
import argparse
import os


SECTION_RE = re.compile(r'^\[([^]]+)\]\s*$')
STAGE_MAP = {
    'vertex shader': 'vert',
    'tessellation control shader': 'tesc',
    'tessellation evaluation shader': 'tese',
    'geometry shader': 'geom',
    'fragment shader': 'frag',
    'compute shader': 'comp',
}

class Converter:
    def __init__(self, type, stage, fout):
        self._type = type
        self._stage = stage
        self._fout = fout
        self._tempfile = tempfile.NamedTemporaryFile('w+')

    def add_line(self, line):
        self._tempfile.write(line)

    def finish(self):
        self._tempfile.flush()

        with tempfile.NamedTemporaryFile() as temp_outfile:
            if self._type == 'glsl':
                subprocess.check_call(["glslangValidator",
                                       "-S", self._stage,
                                       "-G",
                                       "-V",
                                       "-o", temp_outfile.name,
                                       self._tempfile.name])
            else:
                subprocess.check_call(["spirv-as",
                                       "-o", temp_outfile.name,
                                       self._tempfile.name])

            data = temp_outfile.read()

        self._tempfile.close()

        print(base64.b64encode(data).decode('ascii'), file=self._fout)
        print(file=self._fout)


def convert_stream(fin, fout):
    converter = None

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
                                      fout)
                print("[{} binary]".format(stage_name), file=fout)
            elif section_name in STAGE_MAP:
                converter = Converter('glsl',
                                      STAGE_MAP[section_name],
                                      fout)
                print("[{} binary]".format(section_name), file=fout)
            else:
                fout.write(line)
        elif converter:
            converter.add_line(line)
        else:
            fout.write(line)

    if converter:
        converter.finish()



parser = argparse.ArgumentParser(description='Precompile VkRunner scripts.')
parser.add_argument('inputs', metavar='INPUT', type=str, nargs='+',
                    help='an input file process')
parser.add_argument('-o', dest='output', metavar='OUTPUT',
                    help='an output file or directory', required=True)

args = parser.parse_args()
output_is_directory = len(args.inputs) >= 2 or os.path.isdir(args.output)

for input in args.inputs:
    if output_is_directory:
        output = os.path.join(args.output, os.path.basename(input))
    else:
        output = args.output

    with open(input, 'r') as fin:
        with open(output, 'w') as fout:
            convert_stream(fin, fout)
