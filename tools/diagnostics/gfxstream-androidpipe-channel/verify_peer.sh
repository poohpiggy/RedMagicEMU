#!/bin/sh
set -eu
# Fixed source/build root, NDK root, existing AndroidPipe object, fresh output directory.
test "$#" -eq 4
root=$1
ndk=$2
pipe_object=$3
out=$4
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -d "$out"
inc1=$root/prefix/include
inc2=$root/prefix/include/aemu/host-common
inc3=$root/src/host/gl/gl-host-common/include
inc4=$root/aemu/host-common/include/host-common
lib=$root/prefix/lib
export LD_LIBRARY_PATH="$lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$root/bin/cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
  "$here/channel.cpp" "$here/socket_session.cpp" "$here/socket_server.cpp" "$here/socket_test.cpp" \
  "$here/shared_memory.cpp" \
  "$here/../gfxstream-principal-scope/principal_scope.cpp" \
  "$pipe_object" "$root/aemu/host-common/VmLock.cpp" -L"$lib" -Wl,-rpath,"$lib" \
  -Wl,--export-dynamic -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/socket_test"
"$out/socket_test"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++" \
  -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
  -c "$here/socket_session.cpp" -o "$out/socket_session-android-arm64.o"
echo 'PASS: authorized peer retained per socket session; Android ARM64 object compiled'
