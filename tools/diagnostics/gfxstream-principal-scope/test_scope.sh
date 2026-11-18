#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=$1
test -d "$out"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
  "$here/principal_scope.cpp" "$here/scope_test.cpp" -o "$out/scope_test"
"$out/scope_test"
echo 'PASS: one-shot scopes, nesting, unwinding, concurrency and retained lifetime'
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
  "$here/principal_scope.cpp" "$here/reentry_test.cpp" -o "$out/reentry_test"
"$out/reentry_test"
echo 'LIMITATION: an unscoped same-thread reentrant call can consume the token'
