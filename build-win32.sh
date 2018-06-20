#!/bin/bash

set -e

CONFIG_GUESS_URL="http://git.savannah.gnu.org/gitweb/?p=automake.git;a=blob_plain;f=lib/config.guess"

SRC_DIR=$(cd $(dirname "$0") && pwd)
BUILD_DIR="$SRC_DIR/build-win32"
DOWNLOADS_DIR="$BUILD_DIR/downloads"
DEPS_DIR="$BUILD_DIR/deps"
INSTALL_DIR="$BUILD_DIR/install"
RESULT_FILE="$BUILD_DIR/vkrunner.zip"
RESULT_DIR="$BUILD_DIR/result"

function find_compiler ()
{
    local gccbin fullpath;

    if [ -z "$MINGW_TOOL_PREFIX" ]; then
	for gccbin in x86_64{-pc,-w64,}-mingw32{,msvc}-gcc; do
	    fullpath=`which $gccbin 2>/dev/null || true`;
	    if [ -n "$fullpath" -a -e "$fullpath" ]; then
		MINGW_TOOL_PREFIX="${fullpath%%gcc}";
		break;
	    fi;
	done;
	if [ -z "$MINGW_TOOL_PREFIX" ]; then
	    echo;
	    echo "No suitable cross compiler was found.";
	    echo;
	    echo "If you already have a compiler installed,";
	    echo "please set the MINGW_TOOL_PREFIX variable";
	    echo "to point to its location without the";
	    echo "gcc suffix (eg: \"/usr/bin/i386-mingw32-\").";
	    echo;
	    echo "If you are using Ubuntu, you can install a";
	    echo "compiler by typing:";
	    echo;
	    echo " sudo apt-get install mingw32";
	    echo;
	    echo "Otherwise you can try following the instructions here:";
	    echo;
	    echo " http://www.libsdl.org/extras/win32/cross/README.txt";

	    exit 1;
	fi;
    fi;

    TARGET="${MINGW_TOOL_PREFIX##*/}";
    TARGET="${TARGET%%-}";

    echo "Using compiler ${MINGW_TOOL_PREFIX}gcc and target $TARGET";
}

mkdir -p "$DOWNLOADS_DIR"

rm -rf "$DEPS_DIR"
rm -rf "$INSTALL_DIR"
rm -rf "$RESULT_DIR" "$RESULT_FILE"
mkdir -p "$DEPS_DIR"
mkdir -p "$RESULT_DIR"
mkdir -p "$INSTALL_DIR/lib/pkgconfig"

function do_download ()
{
    local local_fn="$DOWNLOADS_DIR/$2"
    if ! test -f "$local_fn"; then
        curl -L -o "$local_fn" "$1"
    fi
}

do_download "$CONFIG_GUESS_URL" "config.guess"

find_compiler
BUILD=`bash $DOWNLOADS_DIR/config.guess`

RUN_PKG_CONFIG="$DEPS_DIR/run-pkg-config.sh";

echo "Generating $DEPS_DIR/run-pkg-config.sh";

cat > "$RUN_PKG_CONFIG" <<EOF
# This is a wrapper script for pkg-config that overrides the
# PKG_CONFIG_LIBDIR variable so that it won't pick up the local system
# .pc files.

# The MinGW compiler on Fedora tries to do a similar thing except that
# it also unsets PKG_CONFIG_PATH. This breaks any attempts to add a
# local search path so we need to avoid using that script.

export PKG_CONFIG_LIBDIR="$INSTALL_DIR/lib/pkgconfig"

exec pkg-config "\$@"
EOF

chmod a+x "$RUN_PKG_CONFIG";

echo "Looking for Vulkan SDK in Wine C drive"

vulkan_lib=$(find ~/.wine/drive_c/VulkanSDK \
                  \( -name vulkan-1.lib -print \) -o \
                  \( -name Source -prune \) | head -n 1)
if test -z "$vulkan_lib"; then
    echo "Couldn't find Vulkan SDK. Try installing it with Wine."
    exit 1
fi

vulkan_root=$(cd $(dirname "$vulkan_lib")/.. && pwd)

cat > "$INSTALL_DIR/lib/pkgconfig/vulkan.pc" <<EOF
prefix=$vulkan_root
exec_prefix=$vulkan_root
libdir=\${exec_prefix}/Lib
includedir=\${prefix}/Include

Name: VULKAN
Description: Vulkan Loader
Version: 1.0.61
Libs: -L\${libdir} -lvulkan-1
Cflags: -I\${includedir}
EOF

cd "$BUILD_DIR"

cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
      -DCMAKE_C_FLAGS="-mms-bitfields -I$INSTALL_DIR/include -O3" \
      -DPKG_CONFIG_EXECUTABLE="$RUN_PKG_CONFIG" \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER=${MINGW_TOOL_PREFIX}gcc \
      -DCMAKE_CXX_COMPILER=${MINGW_TOOL_PREFIX}g++ \
      "$SRC_DIR"

make -j4
make install

cp "$INSTALL_DIR/bin/vkrunner.exe" "$RESULT_DIR"
cp -r "$SRC_DIR/examples" "$RESULT_DIR"
cp "$SRC_DIR/"{README.md,COPYING} "$RESULT_DIR"

cd "$RESULT_DIR"
zip -r "$RESULT_FILE" *
