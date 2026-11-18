#!/bin/sh
# SPDX-License-Identifier: MIT
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
tmp=$(mktemp -d /var/tmp/rme-gles-rebind.XXXXXXXX) || exit 2
echo "artifacts=$tmp"
"${CC:-cc}" -std=gnu11 -O0 -g -Wall -Wno-unused-function -Wno-unused-variable \
    -Wl,--wrap=write -Wl,--wrap=read -pthread "$here/rebind_test.c" \
    -ldl -o "$tmp/rebind_test" >"$tmp/build.log" 2>&1 || { cat "$tmp/build.log" >&2; exit 2; }
failed=0
for case_name in array element element-vao attrib-float attrib-int; do
    echo "CASE $case_name"
    timeout -k 2 15 "$tmp/rebind_test" "$case_name" || failed=1
done
[ "$failed" -eq 0 ] || exit 1
echo GLES_BIND_FORWARD_OK
