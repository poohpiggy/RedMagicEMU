#!/bin/sh
# SPDX-License-Identifier: MIT
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
tmp=$(mktemp -d /var/tmp/rme-unity-map-log.XXXXXXXX) || exit 2
echo "artifacts=$tmp"
"${CC:-cc}" -std=gnu11 -O0 -g -Wall -Wno-unused-function -Wno-unused-variable \
    -Wl,--wrap=write -Wl,--wrap=read -Wl,--wrap=malloc -Wl,--wrap=calloc \
    -pthread "$here/log_test.c" -ldl -o "$tmp/test" \
    >"$tmp/build.log" 2>&1 || { cat "$tmp/build.log" >&2; exit 2; }
failed=0
for reason in no_group unbound invalid_offset invalid_length length_limit range_overflow disconnected \
    binding_unknown name_mapped retired_name slot_exhausted allocation_failed unsupported_flags_busy \
    legacy_mapping legacy_map_unavailable legacy_allocation_failed; do
    echo "CASE $reason"
    timeout -k 2 15 bash -c 'exec -a "$1" "$2" "$3"' _ \
        com.antutu.benchmark.full.lite:unity "$tmp/test" "$reason" || failed=1
done
[ "$failed" -eq 0 ] || exit 1
echo UNITY_MAP_LOG_TEST_OK
