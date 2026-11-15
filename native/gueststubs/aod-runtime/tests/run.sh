#!/bin/sh
set -eu
ulimit -c 0 || true
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rme-aod-tests.XXXXXX")
cleanup() { rm -f -- "$work/runtime" "$work/preprocess" "$work/ort"; rmdir -- "$work"; }
trap cleanup EXIT HUP INT TERM
cc=${CC:-cc}
flags=${CFLAGS:--std=c11 -O2 -Wall -Wextra -Werror}
# CFLAGS is intentionally word-split so callers can choose their compiler flags.
# shellcheck disable=SC2086
"$cc" $flags "$here/aod_runtime.c" "$here/tests/runtime_test.c" -lm -o "$work/runtime"
"$work/runtime"
# shellcheck disable=SC2086
"$cc" $flags "$here/aod_runtime.c" "$here/tests/preprocess_test.c" -lm -o "$work/preprocess"
"$work/preprocess"
if [ "$#" -eq 0 ]; then exit 0; fi
if [ "$#" -ne 5 ] || [ "$1" != "--ort" ]; then
    echo "usage: $0 [--ort HEADER_DIR ORT_SHARED_LIBRARY MODEL_512 MODEL_WRONG_SHAPE]" >&2
    exit 2
fi
shift
# shellcheck disable=SC2086
"$cc" $flags -I "$1" "$here/aod_runtime.c" "$here/aod_ort.c" "$here/tests/ort_integration_test.c" -lm -ldl -o "$work/ort"
"$work/ort" "$2" "$3" "$4"
