#!/bin/sh
# SPDX-License-Identifier: MIT
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
tmp=$(mktemp -d /var/tmp/rme-unity-map.XXXXXXXX) || exit 2
echo "artifacts=$tmp"
sanitizer=
if [ "${RME_SANITIZE:-}" = address ]; then sanitizer=-fsanitize=address; fi
# shellcheck disable=SC2086
"${CC:-cc}" -std=gnu11 -O0 -g -Wall -Wno-unused-function -Wno-unused-variable \
    $sanitizer -fno-omit-frame-pointer -Wl,--wrap=write -Wl,--wrap=read \
    -pthread "$here/test.c" -ldl -o "$tmp/test" >"$tmp/build.log" 2>&1 || { cat "$tmp/build.log" >&2; exit 2; }
failed=0
for case_name in selected explicit-flush write-invalidate mixed-flags shared-lifetime \
    delete-reuse-lifetime ranges unwritten delete-before-map unselected; do
    echo "CASE $case_name"
    program=com.antutu.benchmark.full.lite:unity
    [ "$case_name" != unselected ] || program=not-antutu
    timeout -k 2 15 bash -c 'exec -a "$1" "$2" "$3"' _ "$program" "$tmp/test" "$case_name" || failed=1
done
[ "$failed" -eq 0 ] || exit 1
echo UNITY_MAP_EXPERIMENT_TESTS_OK
