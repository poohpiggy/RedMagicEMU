#!/bin/sh
set -eu
test "$#" -eq 4
root=$1
ndk=$2
pipe_object=$3
out=$4
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
adapter=$here/../gfxstream-androidpipe-channel
test -d "$out"
inc1=$root/prefix/include
inc2=$root/prefix/include/aemu/host-common
inc3=$root/src/host/gl/gl-host-common/include
inc4=$root/aemu/host-common/include/host-common
lib=$root/prefix/lib
export LD_LIBRARY_PATH="$lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$root/bin/cxx" -std=c++17 -Wall -Wextra -Werror -pthread -I"$adapter" \
  -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
  "$here/connector_test.cpp" "$here/pipe_principal_registry.cpp" "$adapter/channel.cpp" \
  "$pipe_object" "$root/aemu/host-common/VmLock.cpp" -L"$lib" -Wl,-rpath,"$lib" \
  -Wl,--export-dynamic -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging \
  -o "$out/connector_test"
"$out/connector_test"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++" \
  -std=c++17 -Wall -Wextra -Werror -fPIC -c "$here/pipe_principal_registry.cpp" -o "$out/registry-android-arm64.o"
echo 'PASS: real AndroidPipe connector with explicit substitute renderer arguments; ARM64 registry compiled'
