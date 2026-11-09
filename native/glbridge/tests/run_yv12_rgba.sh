#!/bin/sh
set -eu
ulimit -c 0
cd "$(dirname "$0")"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/rme-yv12-test.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror yv12_rgba_test.c -o "$tmp/test"
"$tmp/test"
