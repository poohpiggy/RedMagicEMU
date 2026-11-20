#!/bin/sh
set -eu
# Arguments: fixed gfxstream build root, Android NDK root, output directory.
if [ "$#" -ne 3 ]; then
    echo "usage: $0 <gfxstream-build-root> <ndk-root> <output-directory>" >&2
    exit 2
fi
root=$1
ndk=$2
out=$3
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -d "$out"
cxx=$root/bin/cxx
android_cxx=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++
inc1=$root/prefix/include
inc2=$root/prefix/include/aemu/host-common
inc3=$root/src/host/gl/gl-host-common/include
inc4=$root/aemu/host-common/include/host-common
lib=$root/prefix/lib
export LD_LIBRARY_PATH="$lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 "$here/waker_patch.py" "$root/aemu/host-common/AndroidPipe.cpp" "$out/AndroidPipe.cpp"
"$cxx" -std=c++17 -fPIC -I"$here" -I"$inc4" -I"$root/aemu/base/include" \
    -I"$root/aemu/host-common/include" -I"$root/aemu/snapshot/include" -I"$root/aemu" \
    -c "$out/AndroidPipe.cpp" -o "$out/AndroidPipe.o"
"$cxx" -std=c++17 -DBASELINE -isystem "$inc1" -isystem "$inc2" -isystem "$inc4" \
    "$here/waker_test.cpp" "$root/aemu/host-common/VmLock.cpp" -L"$lib" \
    -Wl,-rpath,"$lib" -laemu-host-common -laemu-base -pthread -o "$out/waker_baseline"
if "$out/waker_baseline"; then
    echo "baseline unexpectedly delivered the event" >&2
    exit 1
else
    result=$?
    test "$result" -eq 1 || exit "$result"
fi
"$cxx" -std=c++17 -isystem "$inc1" -isystem "$inc2" -isystem "$inc4" \
    "$here/waker_test.cpp" "$out/AndroidPipe.o" "$root/aemu/host-common/VmLock.cpp" \
    -L"$lib" -Wl,-rpath,"$lib" -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/waker_candidate"
"$out/waker_candidate"
echo 'REAL_PIPE_WAKE_RED_GREEN_OK'
"$cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    "$here/channel.cpp" "$here/channel_test.cpp" "$out/AndroidPipe.o" "$root/aemu/host-common/VmLock.cpp" \
    -L"$lib" -Wl,-rpath,"$lib" -Wl,--export-dynamic -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/channel_test"
"$out/channel_test"
for test in socket_test socket_server_test; do
    "$cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
        "$here/channel.cpp" "$here/socket_session.cpp" "$here/socket_server.cpp" "$here/$test.cpp" \
        "$here/shared_memory.cpp" \
        "$here/../gfxstream-principal-scope/principal_scope.cpp" \
        "$out/AndroidPipe.o" "$root/aemu/host-common/VmLock.cpp" -L"$lib" -Wl,-rpath,"$lib" \
        -Wl,--export-dynamic -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/$test"
    "$out/$test"
done
"$cxx" -std=c++17 -Wall -Wextra -Werror "$here/vulkan_socket_test.cpp" -pthread -o "$out/vulkan_socket_test"
"$out/vulkan_socket_test"
python3 - "$here/../../../native/hwchal/hwc.c" "$out/hwc_query.inc" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
start_marker = 'static int hwc_query('
end_marker = 'static void hwc_register_procs'
if source.count(start_marker) != 1 or source.count(end_marker) != 1:
    raise SystemExit('unexpected HWC query source')
start = source.index(start_marker)
end = source.index(end_marker, start)
Path(sys.argv[2]).write_text(source[start:end])
PY
"$cxx" -std=c++17 -Wall -Wextra -Werror -I"$out" -isystem "$inc1" -isystem "$inc2" -isystem "$inc4" \
    "$here/primary_display.cpp" "$here/empty_color_registry_test.cpp" "$here/primary_display_test.cpp" -pthread -o "$out/primary_display_test"
"$out/primary_display_test"
"$cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc4" \
    "$here/vm_snapshot.cpp" "$here/vm_snapshot_test.cpp" -pthread -o "$out/vm_snapshot_test"
"$out/vm_snapshot_test"
python3 - "$root/src/host" "$out" <<'PY'
from pathlib import Path
import hashlib, sys
root, out = map(Path, sys.argv[1:])
expected = {
    'FrameBuffer.cpp': 'f1e0fa1f5e5bf034f882159f7bdb14ed7c381afa623bc4c02059a3f493790fa4',
    'PostWorker.cpp': '8aa0f4f7b2f2d72247c3740aba65fd330074e89bab39328e0c3c12697b8278a9',
    'vulkan/PostWorkerVk.cpp': '7e7e24f83bc1933f7768daf992e633ddc5d9b83d55b82d7efb291adfac7e91e1',
    'gl/gl-host-common/opengles.cpp': '582dafcd790cf9da31526b8da6c7a8967c9f78916eb72d86b568e923b50c4575',
}
texts = {}
for name, digest in expected.items():
    data = (root / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit('unexpected gfxstream source: ' + name)
    texts[name] = data.decode('utf-8')
frame = texts['FrameBuffer.cpp']
start = frame.index('bool postOnlyOnMainThread() {')
end = frame.index('\n}', start) + 2
(out / 'post_only.inc').write_text(frame[start:end])
start = frame.index('ColorBufferPtr FrameBuffer::findColorBuffer(')
end = frame.index('\n}', start) + 2
(out / 'framebuffer_find.inc').write_text(frame[start:end])
if frame.count('get_emugl_window_operations().isRunningInUiThread()') != 2 or \
   'if (shouldPostOnlyOnMainThread && (PostCmd::Screenshot == post.cmd) &&' not in frame or \
   'if (!shouldPostOnlyOnMainThread ||' not in frame:
    raise SystemExit('unexpected window callback call sites')
post = texts['PostWorker.cpp']
start = post.index('void PostWorker::runTask(')
end = post.index('\n}\n\nbool PostWorker::isComposeTargetReady', start) + 2
(out / 'run_task.inc').write_text(post[start:end])
if 'PostWorker(false, fb, compositor)' not in texts['vulkan/PostWorkerVk.cpp']:
    raise SystemExit('unexpected Vulkan post worker initialization')
start_fn = texts['gl/gl-host-common/opengles.cpp'].split('android_startOpenglesRenderer(', 1)[1]
if start_fn.index('if (!sRenderLib)') > start_fn.index('const GpuInfoList& gpuList'):
    raise SystemExit('renderer start does not reject missing library before GPU query')
PY
"$cxx" -DANDROID -std=c++17 -Wall -Wextra -Werror -I"$out" \
    "$here/android_postworker_test.cpp" -pthread -o "$out/android_postworker_test"
"$out/android_postworker_test"
echo 'ANDROID_POSTWORKER_NO_UI_CALLBACK_OK'
"$cxx" -std=c++17 -Wall -Wextra -Werror -I"$out" -isystem "$inc1" -isystem "$inc2" -isystem "$inc4" \
    -isystem "$inc3" -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    "$here/renderer_start.cpp" "$here/primary_display.cpp" "$here/primary_colorbuffer_test.cpp" -L"$lib" -Wl,-rpath,"$lib" \
    -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/primary_colorbuffer_test"
timeout 10 "$out/primary_colorbuffer_test"
"$cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    "$here/renderer_start.cpp" "$here/primary_display.cpp" "$here/empty_color_registry_test.cpp" "$here/vm_snapshot.cpp" "$here/renderer_start_test.cpp" -L"$lib" -Wl,-rpath,"$lib" \
    -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/renderer_start_test"
"$out/renderer_start_test"
"$cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    "$here/renderer_start.cpp" "$here/primary_display.cpp" "$here/empty_color_registry_test.cpp" "$here/renderer_stop_test.cpp" \
    -L"$lib" -Wl,-rpath,"$lib" -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/renderer_stop_test"
"$out/renderer_stop_test"
"$cxx" -DANDROID -std=c++17 -Wall -Wextra -Werror -I"$here/test_include" -isystem "$root/src/common/vulkan/include" \
    -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    "$here/renderer_start.cpp" "$here/primary_display.cpp" "$here/empty_color_registry_test.cpp" "$here/vm_snapshot.cpp" \
    "$here/renderer_android_agents_test.cpp" "$here/headless_unavailable_test.cpp" -L"$lib" -Wl,-rpath,"$lib" \
    -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -pthread -o "$out/renderer_android_agents_test"
"$out/renderer_android_agents_test"
"$cxx" -DANDROID -std=c++17 -Wall -Wextra -Werror -I"$here/test_include" -isystem "$root/src/common/vulkan/include" \
    -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    "$here/renderer_start.cpp" "$here/renderer_gpu_query_test.cpp" "$here/headless_unavailable_test.cpp" \
    "$here/../gfxstream-android-gpuinfo/query.cpp" "$here/../gfxstream-android-gpuinfo/android_loader.cpp" \
    -L"$lib" -Wl,-rpath,"$lib" -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -ldl -pthread -o "$out/renderer_gpu_query_test"
"$out/renderer_gpu_query_test"
"$cxx" -DANDROID -DRME_TEST_REAL_PRIMARY -std=c++17 -Wall -Wextra -Werror -I"$here/test_include" \
    -isystem "$root/src/common/vulkan/include" -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    "$here/renderer_start.cpp" "$here/renderer_gpu_query_test.cpp" "$here/headless_unavailable_test.cpp" "$here/primary_display.cpp" \
    "$here/empty_color_registry_test.cpp" "$here/vm_snapshot.cpp" \
    "$here/../gfxstream-android-gpuinfo/query.cpp" "$here/../gfxstream-android-gpuinfo/android_loader.cpp" \
    -L"$lib" -Wl,-rpath,"$lib" -lgfxstream_backend -laemu-host-common -laemu-base -laemu-logging -ldl -pthread -o "$out/renderer_primary_gpu_test"
"$out/renderer_primary_gpu_test"
"$cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    "$here/renderer_bootstrap.cpp" "$here/renderer_bootstrap_test.cpp" -o "$out/renderer_bootstrap_test"
"$out/renderer_bootstrap_test"
"$android_cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -c "$here/channel.cpp" -o "$out/channel-android-arm64.o"
"$android_cxx" -std=c++17 -fPIC -I"$here" -I"$inc4" -I"$root/aemu/base/include" \
    -I"$root/aemu/host-common/include" -I"$root/aemu/snapshot/include" -I"$root/aemu" \
    -c "$out/AndroidPipe.cpp" -o "$out/AndroidPipe-android-arm64.o"
for source in socket_session socket_server; do
    "$android_cxx" -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
        -c "$here/$source.cpp" -o "$out/$source-android-arm64.o"
done
"$android_cxx" -DANDROID -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc4" \
    -c "$here/primary_display.cpp" -o "$out/primary_display-android-arm64.o"
"$android_cxx" -DANDROID -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc4" \
    -c "$here/vm_snapshot.cpp" -o "$out/vm_snapshot-android-arm64.o"
"$android_cxx" -DANDROID -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    -c "$here/renderer_start.cpp" -o "$out/renderer_start-android-arm64.o"
"$android_cxx" -DANDROID -std=c++17 -Wall -Wextra -Werror -isystem "$inc1" -isystem "$inc2" -isystem "$inc3" -isystem "$inc4" \
    -isystem "$root/src/include" -isystem "$root/src/host/features/include" -isystem "$root/aemu/snapshot/include" \
    -c "$here/renderer_bootstrap.cpp" -o "$out/renderer_bootstrap-android-arm64.o"
"$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf" -h "$out/channel-android-arm64.o" | grep -E 'Class:|Machine:'
