#!/usr/bin/env python3
"""Build the actual d7a Android Goldfish Vulkan encoder from source inputs."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

if len(sys.argv) != 7:
    sys.exit("usage: rebuild_encoder_candidates.py SOURCE GENERATED_BUILD AOSP_HEADERS HARDWARE_HEADERS NDK OUTPUT")
source, build, aosp, hardware, ndk, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
guest = source / "guest"
generated = build / "guest"
compiler = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
patch_root = output / "patched-source"
encoder_source = patch_root / "guest/vulkan_enc"
if encoder_source.exists():
    sys.exit("patched-source already exists; select a new output directory")
encoder_source.parent.mkdir(parents=True, exist_ok=True)
shutil.copytree(guest / "vulkan_enc", encoder_source)
for name in ("egl", "OpenglSystemCommon"):
    (encoder_source.parent / name).symlink_to((guest / name).resolve(), target_is_directory=True)
patch = Path(__file__).resolve().parents[3] / "tools/diagnostics/gfxstream-acquire-fence/fix.patch"
subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(patch_root), "-i", str(patch)], check=True)
patch = Path(__file__).resolve().parents[3] / "tools/diagnostics/gfxstream-native-buffer-validation/fix.patch"
subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(patch_root), "-i", str(patch)], check=True)
patch = Path(__file__).resolve().parents[3] / "tools/diagnostics/gfxstream-d7a-fence-fd/fix.patch"
subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(patch_root), "-i", str(patch)], check=True)
includes = [
    hardware, guest / "vulkan", encoder_source, guest / "mesa/include",
    guest / "mesa/src", guest / "mesa/src/util", guest / "mesa/src/vulkan/runtime",
    guest / "mesa/src/vulkan/util", guest / "OpenglSystemCommon", guest / "OpenglCodecCommon",
    guest / "OpenglCodecCommon/include", guest / "iostream/include", guest / "qemupipe/include",
    guest / "qemupipe/include-types", guest / "android-emu", guest / "android/include",
    guest / "include", guest / "renderControl_enc", guest / "GoldfishAddressSpace/include",
    guest / "platform/include", guest / "gralloc_cb/include", guest / "GLESv1_enc",
    guest / "GLESv2_enc", generated / "vulkan_enc", generated / "mesa/src/vulkan/util",
    generated / "mesa/src/vulkan/runtime", source / "common/vulkan/include",
    source / "common/opengl/include", aosp / "aosp-liblog-includes", aosp / "aosp-libcutils-includes",
    aosp / "aosp-libbase-files/include", aosp / "aosp-nativewindow", aosp / "aosp-core/libsystem/include",
    aosp / "src/subprojects/libdrm-2.4.133/include/drm",
]
common = [str(compiler), "-O2", "-std=gnu++20", "-fPIC", "-DANDROID", "-DANDROID_API_LEVEL=35",
          "-DVK_USE_PLATFORM_ANDROID_KHR", "-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT",
          "-DHAVE_STRUCT_TIMESPEC", "-DHAVE_PTHREAD", "-DHAVE_THRD_CREATE", "-DHAVE_LINUX_FUTEX_H"]
common += ["-I" + str(path) for path in includes]
common += ["-DGFXSTREAM_ENABLE_GUEST_GOLDFISH=1", '-DLOG_TAG="goldfish_vulkan"',
           "-DVK_ANDROID_native_buffer", "-DVK_EXT_device_memory_report", "-DVK_GOOGLE_gfxstream",
           "-DVK_NO_PROTOTYPES", "-fstrict-aliasing", "-Werror", "-Wno-missing-field-initializers",
           "-Wno-unused-parameter", "-DHAVE_TIMESPEC_GET"]
names = ["AndroidHardwareBuffer", "CommandBufferStagingStream", "DescriptorSetVirtualization",
         "HostVisibleMemoryVirtualization", "ResourceTracker", "Resources", "Validation", "VkEncoder",
         "VulkanHandleMapping", "VulkanStreamGuest", "func_table", "gfxstream_vk_private",
         "goldfish_vk_counting_guest", "goldfish_vk_deepcopy_guest", "goldfish_vk_extension_structs_guest",
         "goldfish_vk_marshaling_guest", "goldfish_vk_reserved_marshaling_guest", "goldfish_vk_transform_guest"]

def build_one(name):
    stem = "android1-" + name
    args = common + ["-MD", "-MF", str(output / (stem + ".d")), "-c",
                     str(encoder_source / (name + ".cpp")), "-o", str(output / (stem + ".o"))]
    (output / (stem + ".command.txt")).write_text(shlex.join(args) + "\n")
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / (stem + ".log")).write_text(result.stdout)
    if result.returncode == 0 and "android_stub/" in (output / (stem + ".d")).read_text():
        raise ValueError(f"unexpected Mesa stub header: {name}")
    return name, result.returncode

with ThreadPoolExecutor(max_workers=4) as pool:
    results = list(pool.map(build_one, names))
for name, status in results:
    print(f"{name}: {status}", flush=True)
if any(status for _, status in results):
    sys.exit(1)
