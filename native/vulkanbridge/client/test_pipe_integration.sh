#!/bin/sh
set -eu
# Local test only: link the actual EMU client to the same-revision AndroidPipe
# process service. The backend is intentionally not initialized as a renderer.
root=${1:?gfxstream build root required}
pipe_object=${2:?patched AndroidPipe object required}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../.." && pwd)
channel="$repo/tools/diagnostics/gfxstream-androidpipe-channel"
principal="$repo/tools/diagnostics/gfxstream-principal-scope"
out=$(mktemp -d /tmp/rme-vk-client-pipe-XXXXXX)
mkdir -p "$out/dev/socket"
path="$out/dev/socket/aemuvk"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -fPIC \
    "-DRME_VK_SOCKET_PATH=\"$path\"" -c "$here/emu_pipe.c" -o "$out/emu_pipe.o"
"$root/bin/cxx" -std=c++17 -Wall -Wextra -Werror -I"$channel" -I"$principal" \
    -isystem "$root/prefix/include" -isystem "$root/prefix/include/aemu/host-common" \
    -isystem "$root/src/host/gl/gl-host-common/include" -isystem "$root/aemu/host-common/include/host-common" \
    "$channel/channel.cpp" "$channel/socket_session.cpp" "$channel/socket_server.cpp" \
    "$channel/shared_memory.cpp" \
    "$principal/principal_scope.cpp" \
    "$here/test_pipe_integration.cpp" "$out/emu_pipe.o" "$pipe_object" "$root/aemu/host-common/VmLock.cpp" \
    -L"$root/prefix/lib" -Wl,-rpath,"$root/prefix/lib" -Wl,--export-dynamic \
    -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/test"
LD_LIBRARY_PATH="$root/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" timeout 10 "$out/test" "$path"
rm "$path" "$out/emu_pipe.o" "$out/test"
rmdir "$out/dev/socket" "$out/dev" "$out"
