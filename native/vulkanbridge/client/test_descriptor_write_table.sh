#!/bin/sh
set -eu
gfx=${1:?gfxstream source required}
encoder=${2:?generated encoder source required}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/util" "$work/log"
# The standalone Linux test links the two real guest translation units.
# Their platform logging macros are the only Android-specific dependency here.
cat > "$work/util/log.h" <<'EOF'
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define mesa_loge(...) std::fprintf(stderr, __VA_ARGS__)
#define ALOGE(...) std::fprintf(stderr, __VA_ARGS__)
#define ALOGW(...) std::fprintf(stderr, __VA_ARGS__)
#define ALOGV(...) ((void)0)
EOF
ln -s ../util/log.h "$work/log/log.h"
"${CXX:-c++}" -std=c++17 -O2 -I"$work" -I"$encoder" -I"$gfx/common/vulkan/include" \
    -I"$gfx/guest/android-emu" "$here/test_descriptor_write_table.cpp" \
    "$encoder/Resources.cpp" "$encoder/DescriptorSetVirtualization.cpp" -o "$work/test"
"$work/test"
