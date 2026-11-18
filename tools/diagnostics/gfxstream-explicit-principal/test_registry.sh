#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=$1
test -d "$out"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
  "$here/pipe_principal_registry.cpp" "$here/registry_test.cpp" -o "$out/registry_test"
"$out/registry_test"
echo 'PASS: host pipe registration, rejection, reuse, concurrency and captured lifetime'
