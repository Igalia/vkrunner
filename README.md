# VkRunner

VkRunner is a Vulkan shader tester based on `shader_runner` in
[Piglit](https://piglit.freedesktop.org/). The goal is to make it be
able to test scripts as similar to Piglit’s shader_test format as
possible.

## Building

VkRunner requires the Vulkan headers in order to build. On a Linux
system these can be installed via the standard system packages which
are `libvulkan-dev` on Ubuntu and Debian or `vulkan-headers` on
Fedora. On Windows the header can be found by installing LunarG’s VulkanSDK
from [here](https://www.lunarg.com/vulkan-sdk/).

Additonally VkRunner requires [CMake](https://cmake.org/).

If the Vulkan headers are installed in a non-standard location (as
will be the case for Windows), you can point CMake to it when
configuring the build as follows:

    cmake -E env CFLAGS=-Ic:/path/to/vulkan/include cmake .

Otherwise you can just run CMake as below:

    cmake .

Next type `make` to build the program. You will find the VkRunner
executable under `src/`.

## Running

VkRunner requires glslangValidator to compile GLSL to SPIR-V. It is
invoked on the fly while executing the test. It must either be
available in your path or you can set the variable
`PIGLIT_GLSLANG_VALIDATOR_BINARY` to point to it. It can be obtained
from [here](https://github.com/KhronosGroup/glslang/).

## [test] section:

The `[test]` section supports the following commands:

> draw rect [ortho] [patch] _x_ _y_ _width_ _height_

Draws a rectangle at the given normalised coordinates. The vertices
will be uploaded at vertex input location 0 as a vec3. Remember that
Vulkan’s normalised coordinate system is different from OpenGL’s. If
`ortho` is specified then the coordinates are scaled from the range
[0,window size] to [-1,1] to make it easier to specify the positions
in pixels. If `patch` is given then a patch topology will be used with
a patch size of four.

> draw arrays [indexed] [instanced] _topology_ _firstVertex_ _vertexCount_ [_instanceCount_]

Calls `vkCmdDraw` with the given parameters. The vertex data will be
sourced from the `[vertex data]` section. The _topology_ should be one
of the values of VkPrimitiveTopology minus the VK\_PRIMITIVE\_TOPOLOGY
prefix. Alternatively it can be a GLenum value as used in Piglit.

If `indexed` is specified then `vkCmdDrawIndexed` will be use to draw
the primitive instead. The indices will be sourced from the
`[indices]` section. _vertexCount_ will be used as the index count,
_firstVertex_ becomes the vertex offset and _firstIndex_ will always
be zero.

> compute _x_ _y_ _z_

Dispatch the compute shader with the given parameters.

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
uniform location. The type can be one of int, uint, int8_t, uint8_t,
int16_t, uint16_t, int64_t, uint64_t, float, double, vec[234],
dvec[234], ivec[234], uvec[234], i8vec[234], u8vec[234], i16vec[234],
u16vec[234], i64vec[234], u64vec[234], mat[234]x[234] or
dmat[234]x[234]. Multiple values can be specified to upload values to
an array of that type. If matrices or arrays are specified they are
assumed to have a stride according to the std140 layout rules.

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

> ssbo _binding_ subdata _type_ _offset_ _values_…

Sets a value within a storage buffer. The command is the same as
`uniform ubo` except for the different buffer type.

> ssbo _binding_ _size_

Sets the size of a storage buffer. This is optional if there are ssbo
subdata commands because in that case it will just take the size of
the largest offset.

> probe ssbo _type_ _binding_ _offset_ _comparison_ _values_…

Probes a value in the storage buffer at _binding_. The _comparison_
can be one of `==`, `!=`, `<`, `>=`, `>` or `<=`. If the type has more
than one component then they are compared individually until one of
them fails the comparison.

> clear color _r_ _g_ _b_ _a_

Sets the color to use for subsequent clear commands. Defaults to all
zeros.

> clear depth _value_

Sets the value to clear the depth buffer to in subsequent clear
commands. Defaults to 1.0.

> clear stencil _value_

Sets the value to clear the stencil buffer to in subsequent clear
commands. Defaults to 0.

> clear

Clears the entire framebuffer to the previously set clear color, depth
and stencil values.

> patch parameter vertices _vertices_

Sets the number of control points for tessellation patches in
subsequent draw calls. Defaults to 3.

> topology, primitiveRestartEnable, patchControlPoints,
> depthClampEnable, rasterizerDiscardEnable, polygonMode, cullMode,
> frontFace, depthBiasEnable, depthBiasConstantFactor, depthBiasClamp,
> depthBiasSlopeFactor, lineWidth, logicOpEnable, logicOp,
> blendEnable, srcColorBlendFactor, dstColorBlendFactor, colorBlendOp,
> srcAlphaBlendFactor, dstAlphaBlendFactor, alphaBlendOp,
> colorWriteMask, depthTestEnable, depthWriteEnable, depthCompareOp,
> depthBoundsTestEnable, stencilTestEnable, front.failOp,
> front.passOp, front.depthFailOp, front.compareOp, front.compareMask,
> front.writeMask, front.reference, back.failOp, back.passOp,
> back.depthFailOp, back.compareOp, back.compareMask, back.writeMask,
> back.reference

These properties can be set on a pipeline by specifying their name
followed by a value in the test section. This will affect subsequent
draw calls. If multiple draw calls are issued with different values
for these properties then a separate pipeline will be created for each
set of state. See the `properties.shader_test` example for details.

> _stage_ entrypoint _name_

Sets the entrypoint function to _name_ for the given stage. This will
be used for subsequent draw calls or compute dispatches.

Take a look in the examples directory for more examples.

## [require] section

> _feature_

The `[require]` section can contain names of members from
VkPhysicalDeviceFeatures. These will be searched for when deciding
which physical device to open. If no physical device with the
corresponding requirements can be found then it will report an error.

> _extension_

Any line that is not a feature and contains entirely alphanumeric and
underscore characters is assumed to be a device extension name. This
will be checked for when searching for a suitable device and if no
device with the extension is found then the test will report that it
was skipped. Otherwise the extension will be enabled when creating the
device.

> framebuffer _format_

Use this to specify the format of the framebuffer using a format from
VkFormat minus the VK_FORMAT prefix.

> depthstencil _format_

If this is specified VkRunner will try to add a depth-stencil
attachment to the framebuffer with the given format. Without it no
depth-stencil buffer will be created.

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

## [indices] section

The `[indices]` section just contains a list of indices to use along
with the vertices in `[vertex data]`. It will be used if the `indexed`
option is given to the `draw arrays` test command.

## Command line arguments

    usage: vkrunner [OPTION]... SCRIPT...
    Runs the shader test script SCRIPT
    
    Options:
      -h            Show this help message
      -i IMG        Write the final rendering to IMG as a PPM image
      -d            Show the SPIR-V disassembly
      -D TOK=REPL   Replace occurences of TOK with REPL in the scripts

## Precompiling shaders

As an alternative to specifying the shaders in GLSL or SPIR-V
assembly, the test scripts can contain a hex dump of the SPIR-V. That
way VkRunner does not need to invoke the compiler or assembler to run
the script. This can be useful either to speed up the execution of the
tests or to run them on hardware where installing the compiler is not
practical. VkRunner also includes a Python script to precompile the
test scripts to binary. It can be run for example as below:

    ./precompile-script.py -o compiled-examples examples/*.shader_test
    ./src/vkrunner compiled-examples/*.shader_test

## Library

VkRunner can alternatively be used as a library to integrate it into
another test suite. Running `make install` installs a static library,
a header and a pkg-config file to configure it. An example to use it
could be as follows:

```C
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <vkrunner/vkrunner.h>

int
main(int argc, char **argv)
{
        if (argc != 2) {
                fprintf(stderr, "usage: %s <script>\n", argv[0]);
                return EXIT_FAILURE;
        }

        /* Create a source representing the file */
        struct vr_source *source = vr_source_from_file(argv[1]);

        /* The templating mechanism can be used to replace tokens in
         * the test scripts
         */
        char current_time[64];
        snprintf(current_time, sizeof current_time, "%i", (int) time(NULL));
        vr_source_add_token_replacement(source,
                                        "@CURRENT_TIME@",
                                        current_time);

        /* This executes all of the script and returns a result. The
         * result will be either VR_RESULT_FAIL, VR_RESULT_SKIP or
         * VR_RESULT_PASS.
         */
        struct vr_executor *executor = vr_executor_new();
        enum vr_result result = vr_executor_execute(executor, source);
        vr_executor_free(executor);

        vr_source_free(source);

        printf("Test status is: %s\n",
               vr_result_to_string(result));

        return result == VR_RESULT_FAIL ? EXIT_FAILURE : EXIT_SUCCESS;
}
```

This can by compiled using a command like the following after running
`make install` on VkRunner:

    cc -o myrunner myrunner.c $(pkg-config --cflags --libs vkrunner)

## Android

VkRunner supports Android NDK build which generates the VkRunner static library
for Android.

- Download [Android NDK](https://developer.android.com/ndk/downloads/).
- Run the following commands:

```
export ANDROID_NDK=/path/to/your/ndk

cd <vkrunner-dir>
mkdir build && cd build

mkdir libs
mkdir app

$ANDROID_NDK/ndk-build -C ../android_test     \
                      NDK_PROJECT_PATH=.      \
                      NDK_LIBS_OUT=`pwd`/libs \
                      NDK_APP_OUT=`pwd`/app
```

- You can see the generated shared library in
`build/app/local/arm64-v8a/libvkrunner.a`.
