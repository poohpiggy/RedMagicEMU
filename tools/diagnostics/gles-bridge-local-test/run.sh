#!/bin/sh
# SPDX-License-Identifier: MIT
# Compiles the complete production GLES encoder and tests it without a socket.
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
tmp=$(mktemp -d /var/tmp/rme-real-gles-map.XXXXXXXX) || exit 2
echo "artifacts=$tmp"
sanitizer=
case "${RME_SANITIZE:-}" in
    '') ;;
    address) sanitizer='-fsanitize=address' ;;
    *) echo 'RME_SANITIZE must be empty or address' >&2; exit 2 ;;
esac
# word splitting is intentional: sanitizer is empty or exactly one compiler flag.
# shellcheck disable=SC2086
"${CC:-cc}" -std=gnu11 -O0 -g -Wall -Wno-unused-function -Wno-unused-variable \
    $sanitizer -fno-omit-frame-pointer -Wl,--wrap=write -Wl,--wrap=read \
    -pthread "$here/map_test.c" -ldl -o "$tmp/map_test" >"$tmp/build.log" 2>&1
status=$?
if [ "$status" -ne 0 ]; then cat "$tmp/build.log" >&2; exit "$status"; fi
failed=0
for case_name in same-target cross-target shared destroy-shared explicit-flush delete-reuse \
    reused-name-shared mapped-state invalid-no-binding invalid-negative-offset \
    invalid-negative-length invalid-bounds invalid-bits invalid-access invalid-read-unsync; do
    echo "CASE $case_name"
    timeout -k 2 15 "$tmp/map_test" "$case_name"
    status=$?
    echo "case_exit=$status"
    [ "$status" -eq 0 ] || failed=1
done
if [ "$failed" -ne 0 ]; then
    echo 'PRODUCTION_GLES_MAP_REGRESSION_FAILED' >&2
    exit 1
fi
echo 'PRODUCTION_GLES_MAP_REGRESSION_OK'
