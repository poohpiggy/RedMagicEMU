#!/bin/sh
set -u
ulimit -c 0 2>/dev/null || exit 1
cd "$(dirname "$0")" || exit 1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/rme-gralloc-test.XXXXXXXX") || exit 1
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DAEMU_META gralloc_yuv_test.c -o "$tmp/test" -pthread || exit 1
"$tmp/test"
