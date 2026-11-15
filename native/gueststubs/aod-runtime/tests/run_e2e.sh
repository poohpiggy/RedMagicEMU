#!/bin/sh
set -eu
ulimit -c 0 || true
if [ "$#" -ne 7 ]; then
    echo "usage: $0 HEADER_DIR ORT_SO MODEL INPUT_RGB WIDTH HEIGHT NEW_OUTPUT_BGRA" >&2
    exit 2
fi
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rme-aod-e2e.XXXXXX")
cleanup() { rm -f -- "$work/e2e"; rmdir -- "$work"; }
trap cleanup EXIT HUP INT TERM
cc=${CC:-cc}
flags=${CFLAGS:--std=c11 -O2 -Wall -Wextra -Werror}
# shellcheck disable=SC2086
"$cc" $flags -I "$1" "$here/aod_runtime.c" "$here/aod_ort.c" \
    "$here/../aod/aod_mask.c" "$here/../aod/aod_postprocess.c" "$here/tests/e2e_test.c" \
    -lm -ldl -o "$work/e2e"
"$work/e2e" "$2" "$3" "$4" "$5" "$6" "$7"
