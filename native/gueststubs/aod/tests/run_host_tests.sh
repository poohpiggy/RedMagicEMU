#!/bin/sh
# Run the AOD core and JNI contract tests with a native host C compiler.
# Pass the Android NDK's jni.h path (or its directory), or set ANDROID_NDK_HOME.
set -eu

if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [path-to-Android-NDK-jni.h-or-directory]" >&2
    exit 2
fi

header=${1:-}
if [ -z "$header" ] && [ -n "${ANDROID_NDK_HOME:-}" ]; then
    for candidate in "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/sysroot/usr/include/jni.h; do
        if [ -f "$candidate" ]; then header=$candidate; break; fi
    done
fi
if [ -d "$header" ]; then header=$header/jni.h; fi
if [ -z "$header" ] || [ ! -f "$header" ]; then
    echo "Android NDK jni.h not found; pass its path or set ANDROID_NDK_HOME." >&2
    exit 2
fi
if ! grep -q '^struct JNINativeInterface {' "$header"; then
    echo "The JNI contract test requires the Android NDK jni.h." >&2
    exit 2
fi

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src_dir=$(CDPATH= cd -- "$test_dir/.." && pwd)
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/rme-aod-host-tests.XXXXXX")
cleanup() {
    rm -f -- "$tmp_dir/jni.h" "$tmp_dir/mask" "$tmp_dir/prepare" "$tmp_dir/finish" "$tmp_dir/jni"
    rmdir -- "$tmp_dir"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# Copy only the header: adding the entire Android sysroot to a host build would
# also shadow the host C library headers.
cp -- "$header" "$tmp_dir/jni.h"
compiler=${CC:-cc}
# CFLAGS is conventionally a space-separated list of compiler options.
# shellcheck disable=SC2086
"$compiler" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} "$test_dir/mask_test.c" "$src_dir/aod_mask.c" -o "$tmp_dir/mask"
# shellcheck disable=SC2086
"$compiler" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} "$test_dir/postprocess_test.c" "$src_dir/aod_postprocess.c" -lm -o "$tmp_dir/prepare"
# shellcheck disable=SC2086
"$compiler" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} "$test_dir/finish_test.c" "$src_dir/aod_postprocess.c" -lm -o "$tmp_dir/finish"
# shellcheck disable=SC2086
"$compiler" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$tmp_dir" "$test_dir/jni_contract_test.c" "$test_dir/jni_backend_fake.c" "$src_dir/aod_jni.c" "$src_dir/aod_mask.c" "$src_dir/aod_postprocess.c" -pthread -lm -o "$tmp_dir/jni"

"$tmp_dir/mask"
"$tmp_dir/prepare"
"$tmp_dir/finish"
"$tmp_dir/jni"
