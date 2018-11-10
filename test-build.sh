#!/bin/bash

set -eu

src_dir="$(cd $(dirname "$0") && pwd)"
build_dir="$src_dir/tmp-build"
install_dir="$build_dir/install"

rm -fr -- "$build_dir"

mkdir -p "$build_dir"
cd "$build_dir"

cmake -G Ninja -DCMAKE_INSTALL_PREFIX="$install_dir" "$src_dir"

ninja -C "$build_dir"
ninja -C "$build_dir" install

# Run the built executable with all of the examples and enable the
# validation layer. Verify that nothing was written to the output.
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_standard_validation \
                  "$install_dir/bin/vkrunner" \
                  -q \
                  "$src_dir/examples"/*.shader_test \
                  2>&1 \
    | tee "$build_dir/output.txt"

if grep -q --invert-match '^/tmp' "$build_dir/output.txt"; then
    echo "FAIL VkRunner had output with quiet flag"
    exit 1;
fi

# Extract the example from the README. This will test both that the
# example is still correct and that all of the necessary public
# headers are properly installed.

example_dir="$build_dir/example"
mkdir -p "$example_dir"

sed -rn -e '/^```C\s*$/,/^```\s*$/! b ; s/^```.*// ; p' \
    < "$src_dir/README.md" \
    > "$example_dir/myrunner.c"

export PKG_CONFIG_PATH="$install_dir/lib/pkgconfig"

gcc -Wall -Werror -o "$example_dir/myrunner" "$example_dir/myrunner.c" \
    $(pkg-config vkrunner --cflags --libs)

for script in "$src_dir/examples/"*.shader_test; do
    "$example_dir/myrunner" "$script"
done

echo
echo "Test build succeeded."
