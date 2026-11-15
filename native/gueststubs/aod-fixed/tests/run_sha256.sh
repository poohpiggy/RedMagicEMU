#!/bin/sh
# Runs the standalone SHA-256 vectors on a host or on an explicitly supplied Android runtime.
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "$test_dir/.." && pwd)
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/rme-aod-sha.XXXXXX")
cleanup() {
    rm -f -- "$tmp_dir/sha256_test"
    rmdir -- "$tmp_dir"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

compiler=${RME_SHA_CC:-cc}
"$compiler" -std=c11 -Wall -Wextra -Werror "$test_dir/sha256_test.c" "$source_dir/sha256.c" -o "$tmp_dir/sha256_test"
if [ -z "${RME_SHA_QEMU:-}" ]; then
    "$tmp_dir/sha256_test"
else
    : "${RME_SHA_LINKER:?Set RME_SHA_LINKER to the Android linker64 path}"
    : "${RME_SHA_LIBDIR:?Set RME_SHA_LIBDIR to the Android Bionic library directory}"
    "$RME_SHA_QEMU" -U LD_LIBRARY_PATH -E "LD_LIBRARY_PATH=$RME_SHA_LIBDIR" \
        "$RME_SHA_LINKER" "$tmp_dir/sha256_test"
fi
