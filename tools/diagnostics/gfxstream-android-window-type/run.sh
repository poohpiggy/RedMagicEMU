#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
if (( $# < 3 )); then
    echo "usage: bash run.sh GFXSTREAM_SOURCE ANDROID_NDK HOST_CXX [HOST_CXX_FLAGS...]" >&2
    exit 2
fi
source_dir=$1
ndk=$2
host_cxx=$3
shift 3
host_flags=("$@")
here=$(cd -- "$(dirname -- "$0")" && pwd)
android_cxx=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang++
for path in "$source_dir" "$ndk" "$host_cxx"; do
    [[ $path = /* ]] || { echo "expected absolute path: $path" >&2; exit 2; }
done
[[ -x $android_cxx && -x $host_cxx ]] || { echo 'compiler missing' >&2; exit 2; }
header=host/native_window/include/gfxstream/host/native_sub_window.h
window=host/native_window/native_sub_window_android.cpp
platform=host/include/render-utils/render_api_platform_types.h
verify_input() {
    local path=$1 expected=$2 actual
    [[ -f $source_dir/$path ]] || { echo "missing source: $path" >&2; exit 2; }
    actual=$(sha256sum "$source_dir/$path")
    [[ ${actual%% *} == "$expected" ]] || { echo "unexpected source hash: $path" >&2; exit 2; }
}
verify_input "$header" 747f0b71d00133f3452c1d890bff9db759e0e4952f131d997f7ae2716f18b28d
verify_input "$window" 3e871cd8a286ff9cf439dd67b146f14c42d3327a064b944ab22054038d6a8e66
verify_input "$platform" 6fccbf303cd0bba68286df1b191133ec3729144d957732ad42e084cdefedf5e2
out=$(mktemp -d /var/tmp/rme-gfx-android-window-type.XXXXXXXX)
echo "artifacts=$out"
for path in "$header" "$window" "$platform"; do
    mkdir -p "$out/source/$(dirname -- "$path")"
    cp -- "$source_dir/$path" "$out/source/$path"
done
includes=(-I "$out/source/host/native_window/include" -I "$out/source/host/include")
type_test() {
    local gles=$1
    env -u CPLUS_INCLUDE_PATH -u C_INCLUDE_PATH -u CPATH -u COMPILER_PATH -u LIBRARY_PATH \
        timeout -k 3 60 "$android_cxx" -std=c++17 "${includes[@]}" \
        -DGFXSTREAM_ENABLE_HOST_GLES="$gles" -fsyntax-only "$here/android-type-test.cpp"
}
compile_window() {
    local gles=$1
    env -u CPLUS_INCLUDE_PATH -u C_INCLUDE_PATH -u CPATH -u COMPILER_PATH -u LIBRARY_PATH \
        timeout -k 3 60 "$android_cxx" -std=c++17 "${includes[@]}" \
        -DGFXSTREAM_ENABLE_HOST_GLES="$gles" -c "$out/source/$window" -o "$out/window-$gles.o"
}
linux_control() {
    timeout -k 3 60 "$host_cxx" "${host_flags[@]}" -std=c++17 "${includes[@]}" \
        -DGFXSTREAM_ENABLE_HOST_GLES=0 -fsyntax-only "$here/linux-type-test.cpp"
}
expect_failure() {
    local label=$1 marker=$2
    shift 2
    local status=0
    "$@" >"$out/$label.log" 2>&1 || status=$?
    if (( status != 1 )) || ! grep -Fq -- "$marker" "$out/$label.log" ||
        [[ $(grep -c 'error:' "$out/$label.log" || true) != 1 ]]; then
        cat "$out/$label.log" >&2
        echo "unexpected result: $label exit=$status" >&2
        exit 1
    fi
    echo "$label: expected failure"
}
expect_failure baseline-type-0 'Android EGLNativeWindowType must be ANativeWindow*' type_test 0
expect_failure baseline-window-0 ANativeWindow_release compile_window 0
type_test 1 >"$out/baseline-type-1.log" 2>&1
compile_window 1 >"$out/baseline-window-1.log" 2>&1
linux_control >"$out/baseline-linux.log" 2>&1
patch --batch --forward -p1 -d "$out/source" < "$here/android-egl-type.patch" >"$out/patch.log"
for gles in 0 1; do
    type_test "$gles" >"$out/fixed-type-$gles.log" 2>&1
    compile_window "$gles" >"$out/fixed-window-$gles.log" 2>&1
done
linux_control >"$out/fixed-linux.log" 2>&1
echo 'ANDROID_WINDOW_TYPE_TEST_OK (compile only; no device run)'
