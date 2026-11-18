#!/bin/sh
set -eu
ulimit -c 0
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d -t rme-vulkan-hello.XXXXXXXX)
trap 'rm -rf -- "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread -Wl,--wrap=send -Wl,--wrap=recv -Wl,--wrap=poll "$here/hello_test.c" "$here/../protocol.c" -o "$tmp/hello_test"
timeout 20 "$tmp/hello_test"
