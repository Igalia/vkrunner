#!/bin/bash

set -e

SRC_DIR=$(cd $(dirname "$0") && pwd)
BUILD_DIR="$SRC_DIR/build-win32"
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

rm -rf "$DEPS_DIR"
rm -rf "$INSTALL_DIR"
rm -rf "$RESULT_DIR" "$RESULT_FILE"
mkdir -p "$DEPS_DIR"
mkdir -p "$RESULT_DIR"
mkdir -p "$INSTALL_DIR/lib/pkgconfig"

CROSS_FILE="$DEPS_DIR/windows.cross"

cat > "$CROSS_FILE" <<EOF
[binaries]
c = '${MINGW_TOOL_PREFIX}gcc'
cpp = '${MINGW_TOOL_PREFIX}g++'
ar = '${MINGW_TOOL_PREFIX}ar'
strip = '${MINGW_TOOL_PREFIX}strip'
rust = 'rustc'
exe_wrapper = 'wine64'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

find_compiler

meson setup --cross-file "$CROSS_FILE" \
      -Drust_args="--target x86_64-pc-windows-gnu -C linker=\"${MINGW_TOOL_PREFIX}gcc\"" \
      -Dprefix="$INSTALL_DIR" \
      "$BUILD_DIR" \
      "$SRC_DIR"

ninja -C "$BUILD_DIR"
ninja -C "$BUILD_DIR" install

cp "$INSTALL_DIR/bin/vkrunner.exe" "$RESULT_DIR"
cp -r "$SRC_DIR/examples" "$RESULT_DIR"
cp "$SRC_DIR/"{README.md,COPYING} "$RESULT_DIR"

cd "$RESULT_DIR"
zip -r "$RESULT_FILE" *
