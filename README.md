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

## [test] section:

The `[test]` currently only supports the following commands:

> draw rect [ortho] _x_ _y_ _width_ _height_

Draws a rectangle at the given normalised coordinates. The vertices
will be uploaded at vertex input location 0 as a vec3. Remember that
Vulkan’s normalised coordinate system is different from OpenGL’s. If
`ortho` is specified then the coordinates are scaled from the range
[0,window size] to [-1,1] to make it easier to specify the positions
in pixels.

> draw arrays [instanced] _topology_ _firstVertex_ _vertexCount_ [_instanceCount_]

Calls `vkCmdDraw` with the given parameters. The vertex data will be
sourced from the `[vertex data]` section. The _topology_ should be one
of the values of VkPrimitiveTopology minus the VK\_PRIMITIVE\_TOPOLOGY
prefix. Alternatively it can be a GLenum value as used in Piglit.

> [relative] probe [rect] (rgb|rgba) (_x_, _y_[, _width_, _height_]) (_r_, _g_, _b_[, _a_])

Verifies that a given rectangle matches the given colour. If the
command begins with the keyword `relative` then the coordinates are
normalised from 0.0 to 1.0, otherwise they are pixel coordinates.
Either way the origin is the top-left corner of the image. If `rect`
is not specified then the width and height are set to 1 pixel. The
alpha component of the image can be ignored or not by specifying
either `rgb` or `rgba`.

> probe all (rgb|rgba) _r_ _g_ _b_ [_a_]

The same as above except that it probes the entire window.

> uniform _type_ _offset_ _values_…

Sets a push constant at the given offset. Note that unlike Piglit, the
offset is a byte offset into the push constant buffer rather than a
uniform location. The type can be one of int, uint, float, double,
vec[234], dvec[234], ivec[234], uvec[234], mat[234]x[234] or
dmat[234]x[234]. If matrices are specified they are assumed to have a
stride according to the std140 layout rules.

> uniform ubo _binding_ _type_ _offset_ _values_…

Sets a value within a uniform buffer. The first time a value is set
within a buffer it will be created with the minimum size needed to
contain all of the values set on it via test commands. It will then be
bound to the descriptor set at the given binding point. The rest of
the arguments are the same as for the `uniform` command. Note that the
buffer is just updated by writing into a memory mapped view of it
which means that if you do an update, draw call, update and then
another draw call both draws will use the values from the second
update. This is because the draws are not flushed until the next probe
command or the test completes.

> clear color _r_ _g_ _b_ _a_

Sets the color to use for subsequent clear commands. Defaults to all
zeros.

> clear

Clears the entire framebuffer to the previously set clear color.

> patch parameter vertices _vertices_

Sets the number of control points for tessellation patches in
subsequent draw calls. Defaults to 3.

Take a look in the examples directory for examples.

## [require] section

> _feature_

The `[require]` section can contain names of members from
VkPhysicalDeviceFeatures. These will be searched for when deciding
which physical device to open. If no physical device with the
corresponding requirements can be found then it will report an error.

> framebuffer _format_

Use this to specify the format of the framebuffer using a format from
VkFormat minus the VK_FORMAT prefix.

## Shader sections

Shaders can be stored in sections like `[vertex shader]` just like in
`shader_runner`. Multiple GLSL shaders can be given for a single stage
and they will be linked together via glslangValidator.

Alternatively, the disassembly of the SPIR-V source can be provided
with a section like `[vertex shader spirv]`. This will be assembled
with `spirv-as`. If a SPIR-V section is given for a stage there can be
no other shaders for that stage.

The vertex shader can also be skipped with an empty section called
`[vertex shader passthrough]`. That will create a simple vertex shader
than just copies a vec4 for input location 0 to `gl_Position`.

## [vertex data] section

The `[vertex data]` section is used to specify vertex attributes and
data for use with the draw arrays command. It is similar to Piglit
except that integer locations are used instead of names and matrices
are specifed by using a location within the matrix rather than having
a separate field.

The format consists of a row of column headers followed by any number
of rows of data. Each column header has the form _ATTRLOC_/_FORMAT_
where _ATTRLOC_ is the location of the vertex attribute to be bound to
this column and _FORMAT_ is the name of a VkFormat minus the VK_FORMAT
prefix.

Alternatively the column header can use something closer the Piglit
format like _ATTRLOC_/_GL\_TYPE_/_GLSL\_TYPE_. _GL\_TYPE_ is the GL
type of data that follows (“half”, “float”, “double”, “byte”, “ubyte”,
“short”, “ushort”, “int” or “uint”), _GLSL\_TYPE_ is the GLSL type of
the data (“int”, “uint”, “float”, “double”, “ivec”\*, “uvec”\*,
“vec”\*, “dvec”\*).

The data follows the column headers in space-separated form. “#” can
be used for comments, as in shell scripts. See the
`vertex-data.shader_test` file as an example.

## Command line arguments

    usage: vkrunner [OPTION]... SCRIPT...
    Runs the shader test script SCRIPT
    
    Options:
      -h            Show this help message
      -i IMG        Write the final rendering to IMG as a PPM image
      -d            Show the SPIR-V disassembly
