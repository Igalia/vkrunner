# VkRunner

VkRunner is a Vulkan shader tester based on `shader_runner` in
[Piglit](https://piglit.freedesktop.org/). The goal is to make it be
able to test scripts as similar to Piglit’s shader_test format as
possible.

## Running

VkRunner requires glslangValidator to compile GLSL to SPIR-V. It is
invoked on the fly while executing the test. It must either be
available in your path or you can set the variable
`PIGLIT_GLSLANG_VALIDATOR_BINARY` to point to it. It can be obtained
from [here](https://github.com/KhronosGroup/glslang/).

## Status

VkRunner can compile shaders from shader sections like `[vertex
shader]` as in shader_runner. It doesn’t support the `[require]` or
`[vertex data]` sections. The `[test]` section currently only supports
two commands:

> draw rect _x_ _y_ _width_ _height_

Draws a rectangle at the given normalised coordinates. The vertices
will be uploaded at vertex input location 0 as a vec3. Remember that
Vulkan’s normalised coordinate system is different from OpenGL’s.

> probe rect rgba (_x_, _y_, _width_, _height_) (_r_, _g_, _b_, _a_)

Verifies that a given rectangle (in viewport coordinates) matches the
given colour.

Take a look in the examples directory for examples.

## Command line arguments

    usage: vkrunner [OPTION]... SCRIPT...
    Runs the shader test script SCRIPT
    
    Options:
      -h            Show this help message
      -i IMG        Write the final rendering to IMG as a PPM image
      -d            Show the SPIR-V disassembly
