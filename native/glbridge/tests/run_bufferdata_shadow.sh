#!/bin/sh
# Compile the complete production encoder with only Android system stubs.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
temp=$(mktemp -d /var/tmp/rme-bufferdata-shadow.XXXXXXXX)
trap 'rm -rf "$temp"' EXIT HUP INT TERM
"${CC:-cc}" -std=gnu11 -O0 -g -Wall -Wno-unused-function -Wno-unused-variable \
    -pthread "$here/bufferdata_shadow_test.c" -ldl -o "$temp/test"
"$temp/test"
