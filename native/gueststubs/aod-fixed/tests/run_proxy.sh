#!/bin/sh
set -eu

# Point JNI_HEADER at the unmodified Android NDK jni.h. Only this public header
# is copied into the disposable build directory; no SDK binaries are bundled.
: "${JNI_HEADER:?Set JNI_HEADER to the Android NDK sysroot's jni.h}"
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/rme-aod-fixed-test.XXXXXXXX")
trap 'rm -rf -- "$build"' EXIT HUP INT TERM
cp -- "$JNI_HEADER" "$build/jni.h"
cc=${CC:-cc}
"$cc" -std=c11 -Wall -Wextra -Werror -I"$build" -fPIC -shared "$base/tests/fake_original.c" -o "$build/original.so"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$build" -fPIC -shared -DRME_TEST_OMIT_RELEASE "$base/tests/fake_original.c" -o "$build/missing-symbol.so"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$build" -fPIC -shared -DRME_AOD_TEST_FIXTURE "$base/proxy.c" "$base/sha256.c" -ldl -pthread -o "$build/proxy.so"
"$cc" -std=c11 -Wall -Wextra -Werror -I"$build" "$base/tests/proxy_test.c" -ldl -pthread -rdynamic -o "$build/test"
printf '\200' > "$build/alpha.raw"
printf '\177' > "$build/alpha-bad.raw"
ln -s -- "$build/alpha.raw" "$build/alpha-link.raw"
mkfifo "$build/alpha-fifo.raw"
export RME_TEST_ORIGINAL_PATH="$build/original.so"
export RME_TEST_ALPHA_PATH="$build/alpha.raw"
unset RME_TEST_ORIGINAL_OK RME_TEST_SO_MISSING RME_TEST_STATUS_THROWS RME_TEST_ALPHA_THROWS
failed=0
run_case() { if "$@"; then :; else rc=$?; echo "FAIL case (exit $rc): $*" >&2; failed=1; fi; }
run_case env RME_TEST_ORIGINAL_OK=1 "$build/test" "$build/proxy.so" original
run_case env RME_TEST_ORIGINAL_OK=1 RME_TEST_REENTER=1 RME_TEST_PROXY_PATH="$build/proxy.so" timeout 3 "$build/test" "$build/proxy.so" reenter
run_case env RME_TEST_CONSTRUCTOR_REENTER=1 RME_TEST_PROXY_PATH="$build/proxy.so" timeout 3 "$build/test" "$build/proxy.so" constructor-reenter
run_case timeout 3 "$build/test" "$build/proxy.so" release-during-run
run_case timeout 3 "$build/test" "$build/proxy.so" threaded-release
run_case env RME_TEST_WAIT_INIT=1 timeout 3 "$build/test" "$build/proxy.so" overlapping-init
run_case env RME_TEST_WAIT_INIT=1 timeout 3 "$build/test" "$build/proxy.so" release-during-init
run_case "$build/test" "$build/proxy.so" cache
run_case env RME_TEST_SO_MISSING=1 "$build/test" "$build/proxy.so" no-so
run_case env RME_TEST_ALPHA_PATH="$build/alpha-bad.raw" "$build/test" "$build/proxy.so" invalid
run_case env RME_TEST_ALPHA_PATH="$build/missing.raw" "$build/test" "$build/proxy.so" invalid
run_case env RME_TEST_ORIGINAL_PATH="$build/missing-original.so" "$build/test" "$build/proxy.so" missing-original
run_case env RME_TEST_ORIGINAL_PATH="$build/missing-symbol.so" "$build/test" "$build/proxy.so" missing-original
run_case env RME_TEST_ORIGINAL_PATH="$build/proxy.so" timeout 3 "$build/test" "$build/proxy.so" missing-original
run_case env RME_TEST_STATUS_THROWS=1 "$build/test" "$build/proxy.so" query-throws
run_case env RME_TEST_ALPHA_THROWS=1 "$build/test" "$build/proxy.so" query-throws
run_case env RME_TEST_ALPHA_PATH="$build/alpha-link.raw" "$build/test" "$build/proxy.so" invalid
run_case env RME_TEST_ALPHA_PATH="$build/alpha-fifo.raw" timeout 3 "$build/test" "$build/proxy.so" invalid
exit "$failed"
