#!/bin/sh
set -eu
# Fixed source/build root, NDK root, existing AndroidPipe object, output directory.
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
export LD_LIBRARY_PATH="$out:$lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$root/bin/cxx" -std=c++17 -Wall -Wextra -Werror -fPIC -shared -Wl,-soname,librme-principal-scope.so \
  "$here/principal_scope.cpp" -o "$out/librme-principal-scope.so"
"$root/bin/cxx" -std=c++17 -Wall -Wextra -Werror -pthread -I"$adapter" \
  -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
  "$here/connector_test.cpp" "$adapter/channel.cpp" "$pipe_object" "$root/aemu/host-common/VmLock.cpp" \
  -L"$out" -L"$lib" -Wl,-rpath,"$out" -Wl,-rpath,"$lib" -Wl,--export-dynamic \
  -lrme-principal-scope -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging \
  -o "$out/connector_test"
"$out/connector_test"
for module in scope consume; do
  "$root/bin/cxx" -std=c++17 -Wall -Wextra -Werror -fPIC -shared "$here/module_$module.cpp" \
    -L"$out" -Wl,-rpath,"$out" -lrme-principal-scope -o "$out/libmodule-$module.so"
done
"$root/bin/cxx" -std=c++17 -Wall -Wextra -Werror "$here/module_test.cpp" -L"$out" \
  -Wl,-rpath,"$out" -lmodule-scope -lmodule-consume -lrme-principal-scope -o "$out/module_test"
"$out/module_test"
android_cxx=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++
"$android_cxx" -std=c++17 -Wall -Wextra -Werror -fPIC -c "$here/principal_scope.cpp" -o "$out/principal_scope-android-arm64.o"
echo 'PASS: real AndroidPipe connector and substitute renderer capture before worker start'
echo 'PASS: two local Linux shared modules share one principal TLS library'
