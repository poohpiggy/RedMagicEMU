#!/usr/bin/env python3
"""Compile the real d7a Android.bp stream sources for a link diagnostic."""
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile

if len(sys.argv) not in (5, 6) or len(sys.argv) == 6 and sys.argv[5] != "--emu-vulkan-pipe":
    sys.exit("usage: compile_stream_components.py TEMPLATE_COMMAND SOURCE REAL_HEADERS OUTPUT [--emu-vulkan-pipe]")
template, source, headers, out = map(Path, sys.argv[1:5])
out.mkdir(parents=True, exist_ok=True)
patch_file = Path(__file__).resolve().parents[3] / "tools/diagnostics/gfxstream-process-pipe/fix.patch"
patch_root = tempfile.TemporaryDirectory(prefix="rme-vk-stream-source-")
patched_source = Path(patch_root.name)
patched_files = {"guest/OpenglSystemCommon/ProcessPipe.cpp", "guest/OpenglSystemCommon/HostConnection.cpp"}
for name in patched_files:
    dst = patched_source / name
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / name, dst)
subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch_file)], cwd=patched_source, check=True)
if len(sys.argv) == 6:
    emu_patch = Path(__file__).with_name("host_connection_emu_pipe.patch").resolve()
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(emu_patch)],
                   cwd=patched_source, check=True)
args = shlex.split(template.read_text())
args = [a for a in args if "/android_stub" not in a and not a.startswith("-DLOG_TAG=")]
args.insert(1, "-I" + str(headers))
args.insert(1, "-I" + str(source / "guest/mesa/src/vulkan/wsi"))
args.insert(1, "-I" + str(source / "common/etc/include"))
drm_header_dirs = [Path(a[2:]) for a in args if a.startswith("-I") and a.endswith("/include/drm")]
if drm_header_dirs:
    args.insert(1, "-I" + str(drm_header_dirs[0].parent.parent))
args.insert(1, "-I" + str(source / "guest/platform/include"))
args.insert(1, "-I" + str(source / "guest/OpenglSystemCommon"))
base_dirs = [Path(a[2:]) for a in args if a.startswith("-I") and a.endswith("/aosp-libbase-files/include")]
if base_dirs:
    aosp_root = base_dirs[0].parent.parent
    for path in ("aosp-nativewindow", "aosp-nativebase", "aosp-minigbm", "aosp-core/libsystem/include"):
        args.insert(1, "-I" + str(aosp_root / path))
items = [
    ("guest/android-emu/aemu/base/files/Stream.cpp", "Stream", '"androidemu"'),
    ("guest/android-emu/aemu/base/files/StreamSerializing.cpp", "StreamSerializing", '"androidemu"'),
    ("guest/OpenglCodecCommon/ChecksumCalculator.cpp", "ChecksumCalculator", '"eglCodecCommon"'),
    ("guest/OpenglCodecCommon/glUtils.cpp", "glUtils", '"eglCodecCommon"'),
    ("guest/OpenglCodecCommon/EncoderDebug.cpp", "EncoderDebug", '"eglCodecCommon"'),
    ("guest/OpenglCodecCommon/GLClientState.cpp", "GLClientState", '"eglCodecCommon"'),
    ("guest/OpenglCodecCommon/GLESTextureUtils.cpp", "GLESTextureUtils", '"eglCodecCommon"'),
    ("guest/OpenglCodecCommon/GLSharedGroup.cpp", "GLSharedGroup", '"eglCodecCommon"'),
    ("guest/OpenglCodecCommon/IndexRangeCache.cpp", "IndexRangeCache", '"eglCodecCommon"'),
    ("common/etc/etc.cpp", "etc", '"eglCodecCommon"'),
    ("guest/android-emu/aemu/base/AndroidSubAllocator.cpp", "AndroidSubAllocator", '"androidemu"'),
    ("guest/android-emu/aemu/base/threads/AndroidWorkPool.cpp", "AndroidWorkPool", '"androidemu"'),
    ("guest/android-emu/aemu/base/threads/AndroidThread_pthread.cpp", "AndroidThread", '"androidemu"'),
    ("guest/android-emu/aemu/base/AndroidHealthMonitor.cpp", "AndroidHealthMonitor", '"androidemu"'),
    ("guest/android-emu/aemu/base/AndroidHealthMonitorConsumerBasic.cpp", "AndroidHealthMonitorConsumer", '"androidemu"'),
    ("guest/android-emu/aemu/base/Tracing.cpp", "Tracing", '"androidemu"'),
    ("guest/android-emu/aemu/base/files/MemStream.cpp", "MemStream", '"androidemu"'),
    ("guest/android-emu/aemu/base/synchronization/AndroidMessageChannel.cpp", "AndroidMessageChannel", '"androidemu"'),
    ("guest/android-emu/aemu/base/threads/AndroidFunctorThread.cpp", "AndroidFunctorThread", '"androidemu"'),
    ("guest/android-emu/aemu/base/Process.cpp", "Process", '"androidemu"'),
    ("guest/OpenglSystemCommon/QemuPipeStream.cpp", "QemuPipeStream", '"eglCodecCommon"'),
    ("guest/OpenglSystemCommon/VirtioGpuPipeStream.cpp", "VirtioGpuPipeStream", '"eglCodecCommon"'),
    ("guest/OpenglSystemCommon/ProcessPipe.cpp", "ProcessPipe", '"eglCodecCommon"'),
    ("guest/OpenglSystemCommon/ThreadInfo.cpp", "ThreadInfo", '"eglCodecCommon"'),
    ("guest/OpenglSystemCommon/HostConnection.cpp", "HostConnection", '"eglCodecCommon"'),
    ("guest/vulkan/gfxstream_vk_wsi.cpp", "gfxstream_vk_wsi", '"goldfish_vulkan"'),
    ("guest/renderControl_enc/renderControl_client_context.cpp", "renderControl_client_context", '"eglCodecCommon"'),
    ("guest/renderControl_enc/renderControl_enc.cpp", "renderControl_enc", '"eglCodecCommon"'),
    ("guest/renderControl_enc/renderControl_entry.cpp", "renderControl_entry", '"eglCodecCommon"'),
    ("guest/GoldfishAddressSpace/AddressSpaceStream.cpp", "AddressSpaceStream", '"goldfish-address-space"'),
    ("guest/GoldfishAddressSpace/VirtioGpuAddressSpaceStream.cpp", "VirtioGpuAddressSpaceStream", '"goldfish-address-space"'),
    ("guest/GoldfishAddressSpace/goldfish_address_space.cpp", "goldfish_address_space", '"goldfish-address-space"'),
    ("guest/GoldfishAddressSpace/GoldfishAddressSpaceStream.cpp", "GoldfishAddressSpaceStream", '"goldfish-address-space"'),
    ("guest/platform/VirtGpu.cpp", "VirtGpu", '"platform"'),
    ("guest/platform/linux/LinuxVirtGpuBlob.cpp", "LinuxVirtGpuBlob", '"platform"'),
    ("guest/platform/linux/LinuxVirtGpuBlobMapping.cpp", "LinuxVirtGpuBlobMapping", '"platform"'),
    ("guest/platform/linux/LinuxVirtGpuDevice.cpp", "LinuxVirtGpuDevice", '"platform"'),
    ("guest/platform/linux/LinuxSync.cpp", "LinuxSync", '"platform"'),
    ("guest/android/ANativeWindowAndroid.cpp", "ANativeWindowAndroid", '"eglCodecCommon"'),
    ("guest/android/Gralloc.cpp", "Gralloc", '"eglCodecCommon"'),
    ("guest/android/GrallocGoldfish.cpp", "GrallocGoldfish", '"eglCodecCommon"'),
    ("guest/android/GrallocMinigbm.cpp", "GrallocMinigbm", '"eglCodecCommon"'),
]
for file, name, log_tag in items:
    command = args.copy()
    # The template is from vulkan_enc, whose Android.bp adds -Werror.  The
    # platform and androidemu modules do not inherit that module-specific flag.
    if not file.startswith("guest/vulkan/"):
        command = [arg for arg in command if arg != "-Werror"]
    command.insert(1, "-DLOG_TAG=" + log_tag)
    command[command.index("-c") + 1] = str((patched_source if file in patched_files else source) / file)
    command[command.index("-o") + 1] = str(out / (name + ".o"))
    command[command.index("-MF") + 1] = str(out / (name + ".d"))
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / (name + ".log")).write_text(result.stdout)
    print(f"{name}: {result.returncode}", flush=True)
    if result.returncode:
        print(result.stdout)
        sys.exit(1)
    if "android_stub/" in (out / (name + ".d")).read_text():
        sys.exit("unexpected Mesa stub headers")
patch_root.cleanup()
