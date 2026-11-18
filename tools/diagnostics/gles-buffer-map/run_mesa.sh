#!/bin/sh
# Run the same GLES regression against a local Mesa software renderer.
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <Android-NDK-root>" >&2
    exit 2
fi

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
headers=$1/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include
if [ ! -d "$headers/EGL" ] || [ ! -d "$headers/GLES3" ] || [ ! -d "$headers/KHR" ]; then
    echo "Android NDK EGL/GLES3/KHR headers not found: $headers" >&2
    exit 2
fi

tmpdir=$(mktemp -d -t rme-gles-map-mesa.XXXXXXXX)
trap 'rm -rf -- "$tmpdir"' EXIT HUP INT TERM
cp -R -- "$headers/EGL" "$headers/GLES3" "$headers/KHR" "$tmpdir/"
cc -std=c11 -Wall -Wextra -Werror -I "$tmpdir" "$here/probe.c" \
    -Wl,-l:libEGL.so.1 -Wl,-l:libGL.so.1 -o "$tmpdir/probe"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=true "$tmpdir/probe"
