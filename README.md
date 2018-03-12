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
shader]` as in shader_runner. It doesn’t support the `[vertex data]`
sections.

## [test] section:

The `[test]` currently only supports the following commands:

> draw rect _x_ _y_ _width_ _height_

Draws a rectangle at the given normalised coordinates. The vertices
will be uploaded at vertex input location 0 as a vec3. Remember that
Vulkan’s normalised coordinate system is different from OpenGL’s.

> probe rect rgba (_x_, _y_, _width_, _height_) (_r_, _g_, _b_, _a_)

Verifies that a given rectangle (in viewport coordinates) matches the
given colour.

> probe all rgba _r_ _g_ _b_ _a_

The same as above except that it probes the entire window.

> uniform _type_ _offset_ _values_…

Sets a push constant at the given offset. Note that unlike Piglit, the
offset is a byte offset into the push constant buffer rather than a
uniform location. The type can be one of int, float, double, vec[234],
dvec[234] or ivec[234].

Take a look in the examples directory for examples.

## [require] section

The `[require]` section can contain names of members from
VkPhysicalDeviceFeatures. These will be searched for when deciding
which physical device to open. If no physical device with the
corresponding requirements can be found then it will report an error.

## Command line arguments

    usage: vkrunner [OPTION]... SCRIPT...
    Runs the shader test script SCRIPT
    
    Options:
      -h            Show this help message
      -i IMG        Write the final rendering to IMG as a PPM image
      -d            Show the SPIR-V disassembly
