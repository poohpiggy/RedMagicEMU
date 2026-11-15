#!/bin/sh
set -eu

# Optional: both arguments are private local files and are never copied into Git.
# $1 is the 1216x2688 packed RGB input, $2 the corresponding raw alpha.
: "${JNI_HEADER:?Set JNI_HEADER to the Android NDK sysroot's jni.h}"
if [ "$#" -ne 2 ]; then echo "Usage: run_real_input.sh RGB_FILE ALPHA_FILE" >&2; exit 2; fi
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/rme-aod-fixed-real.XXXXXXXX")
trap 'rm -rf -- "$build"' EXIT HUP INT TERM
cp -- "$JNI_HEADER" "$build/jni.h"
cp -- "$2" "$build/alpha.raw"
cc=${CC:-cc}
unset RME_TEST_ORIGINAL_OK RME_TEST_SO_MISSING
"$cc" -std=c11 -Wall -Wextra -Werror -I"$build" -fPIC -shared \
    "$base/tests/fake_original.c" -o "$build/original.so"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$build" -fPIC -shared \
    "-DRME_AOD_ORIGINAL_PATH=\"$build/original.so\"" "-DRME_AOD_ALPHA_PATH=\"$build/alpha.raw\"" \
    "$base/proxy.c" "$base/sha256.c" -ldl -pthread -o "$build/proxy.so"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$build" "$base/tests/real_input_test.c" -ldl -o "$build/test"
"$build/test" "$1" "$2" valid "$build/proxy.so"
# The first byte is zero in the verified target mask; corrupt only the private copy.
printf '\177' | dd of="$build/alpha.raw" bs=1 count=1 conv=notrunc 2>/dev/null
"$build/test" "$1" "$2" invalid "$build/proxy.so"
