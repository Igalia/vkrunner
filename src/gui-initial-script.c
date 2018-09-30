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

#include "gui-initial-script.h"

const char
gui_initial_script[] =
        "[vertex shader passthrough]\n"
        "\n"
        "[fragment shader]\n"
        "#version 430\n"
        "\n"
        "layout(location = 0) out vec4 frag_color;\n"
        "\n"
        "layout(std140, push_constant) uniform block {\n"
        "        vec4 color;\n"
        "};\n"
        "\n"
        "void\n"
        "main()\n"
        "{\n"
        "        frag_color = color;\n"
        "}\n"
        "\n"
        "[vertex data]\n"
        "0/R32G32_SFLOAT\n"
        "-0.5 -0.5\n"
        "-0.5 0.5\n"
        "0.5 0.5\n"
        "\n"
        "[test]\n"
        "clear\n"
        "\n"
        "uniform vec4 0 1.0 0.0 0.0 1.0 \n"
        "draw arrays TRIANGLE_LIST 0 3\n"
        "\n"
        "uniform vec4 0 0.0 1.0 0.0 1.0 \n"
        "draw rect 0.4 -0.4 0.5 0.5\n"
        "\n"
        "relative probe rect rgb (0.71, 0.31, 0.23, 0.23) (0.0, 1.0, 0.0)\n";
