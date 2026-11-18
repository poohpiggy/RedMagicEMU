#!/usr/bin/env python3
"""Compare fixed gfxstream Android encoder objects with Goldfish macro off/on."""
import argparse
import concurrent.futures
import hashlib
import pathlib
import re
import shlex
import subprocess
import tempfile

NAMES = (
    "AndroidHardwareBuffer", "CommandBufferStagingStream", "DescriptorSetVirtualization",
    "func_table", "goldfish_vk_counting_guest", "goldfish_vk_deepcopy_guest",
    "goldfish_vk_extension_structs_guest", "goldfish_vk_marshaling_guest",
    "goldfish_vk_reserved_marshaling_guest", "goldfish_vk_transform_guest",
    "HostVisibleMemoryVirtualization", "Resources", "ResourceTracker", "Validation",
    "VkEncoder", "VulkanHandleMapping", "VulkanStreamGuest", "gfxstream_vk_private",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    for name in ("source", "generated", "aosp", "ndk", "encoder-dir"):
        ap.add_argument("--" + name, type=pathlib.Path, required=True)
    ap.add_argument("--jobs", type=int, default=2)
    a = ap.parse_args()
    if not 1 <= a.jobs <= 4:
        ap.error("jobs must be between 1 and 4")
    src, generated, aosp, ndk, enc = [p.resolve(strict=True) for p in
        (a.source, a.generated, a.aosp, a.ndk, a.encoder_dir)]
    guest, built = src / "guest", generated / "guest"
    bin_dir = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    cc, nm, readelf = [bin_dir / p for p in ("aarch64-linux-android35-clang++", "llvm-nm", "llvm-readelf")]
    bp, encoder_bp = guest / "Android.bp", guest / "vulkan_enc/Android.bp"
    stub = guest / "mesa/include/android_stub"
    platform = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android"
    overlay = enc / "platform-headers"
    for link, target in ((overlay / "android/hardware_buffer.h", platform / "hardware_buffer.h"),
                         (overlay / "android/data_space.h", platform / "data_space.h"),
                         (overlay / "android/rect.h", platform / "rect.h"),
                         (overlay / "vndk/hardware_buffer.h", aosp / "aosp-nativewindow-full/include/vndk/hardware_buffer.h")):
        if not target.is_file() or link.resolve(strict=True) != target.resolve(strict=True):
            raise RuntimeError(f"unexpected platform header: {link}")
    for p in (cc, nm, readelf, bp, encoder_bp, enc / "objects.tsv"):
        if not p.is_file(): raise FileNotFoundError(p)
    if '"-DGFXSTREAM_ENABLE_GUEST_GOLDFISH=1"' not in bp.read_text(encoding="utf-8"):
        raise RuntimeError("expected Android default macro not found")
    text = encoder_bp.read_text(encoding="utf-8")
    group = re.search(r'filegroup\s*\{\s*name:\s*"vulkan_enc_impl",\s*srcs:\s*\[(.*?)\]', text, re.S)
    if not group or re.findall(r'"([^\"]+)\.cpp"', group.group(1)) != list(NAMES):
        raise RuntimeError("unexpected Android encoder source list")
    extra_flags = ['-DLOG_TAG="goldfish_vulkan"', '-DVK_ANDROID_native_buffer',
                   '-DVK_EXT_device_memory_report', '-DVK_GOOGLE_gfxstream', '-DVK_NO_PROTOTYPES',
                   '-fstrict-aliasing', '-Werror', '-Wno-missing-field-initializers',
                   '-Wno-unused-parameter', '-DHAVE_TIMESPEC_GET']
    if '"libgfxstream_guest_cc_defaults"' not in text or any(
            f'"{flag}"' not in text for flag in extra_flags[1:]):
        raise RuntimeError("expected Android encoder default/flags not found")
    baseline = {}
    for row in (enc / "objects.tsv").read_text(encoding="utf-8").splitlines():
        name, source_hash, object_hash = row.split("\t")
        baseline[name] = (source_hash.removeprefix("source_sha256="), object_hash.removeprefix("object_sha256="))
    if set(baseline) != set(NAMES): raise RuntimeError("unexpected encoder baseline manifest")
    for n in NAMES:
        if digest(guest / "vulkan_enc" / (n + ".cpp")) != baseline[n][0] or digest(enc / (n + ".o")) != baseline[n][1]:
            raise RuntimeError(f"source/object differs from recorded baseline: {n}")
    includes = [guest / "vulkan", guest / "vulkan_enc", guest / "mesa/include", guest / "mesa/src",
        guest / "mesa/src/util", guest / "mesa/src/vulkan/runtime", guest / "mesa/src/vulkan/util",
        guest / "OpenglSystemCommon", guest / "OpenglCodecCommon", guest / "OpenglCodecCommon/include",
        guest / "iostream/include", guest / "qemupipe/include", guest / "qemupipe/include-types",
        guest / "android-emu", guest / "android/include", guest / "include", guest / "renderControl_enc",
        guest / "GoldfishAddressSpace/include", guest / "platform/include", guest / "gralloc_cb/include",
        guest / "GLESv1_enc", guest / "GLESv2_enc", built / "vulkan_enc", built / "mesa/src/vulkan/util",
        built / "mesa/src/vulkan/runtime", src / "common/vulkan/include", src / "common/opengl/include",
        aosp / "aosp-liblog-includes", aosp / "aosp-libcutils-includes", aosp / "aosp-libbase-files/include",
        overlay, aosp / "aosp-core/libsystem/include", stub, aosp / "src/subprojects/libdrm-2.4.133/include/drm"]
    if not all(p.is_dir() for p in includes): raise FileNotFoundError("missing include directory")
    out = pathlib.Path(tempfile.mkdtemp(prefix="rme-vk-goldfish-config-", dir="/var/tmp"))
    print(f"ARTIFACTS={out}", flush=True)
    common = [str(cc), "-std=gnu++20", "-fPIC", "-DANDROID", "-DANDROID_API_LEVEL=35",
              "-DVK_USE_PLATFORM_ANDROID_KHR", "-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT",
              "-DHAVE_STRUCT_TIMESPEC", "-DHAVE_PTHREAD", "-DHAVE_THRD_CREATE",
              f"-ffile-prefix-map={out}=/rme/goldfish-config"]
    common.extend("-I" + str(p) for p in includes)

    def compile_one(pair):
        mode, name = pair
        source = guest / "vulkan_enc" / (name + ".cpp")
        obj, dep = out / f"{mode}-{name}.o", out / f"{mode}-{name}.d"
        goldfish = 0 if mode == "0" else 1
        flags = extra_flags if mode == "android1" else []
        cmd = common + [f"-DGFXSTREAM_ENABLE_GUEST_GOLDFISH={goldfish}", *flags, "-MD", "-MF", str(dep),
                        "-c", str(source), "-o", str(obj)]
        (out / f"{mode}-{name}.command.txt").write_text(shlex.join(cmd) + "\n", encoding="utf-8")
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / f"{mode}-{name}.log").write_text(p.stdout, encoding="utf-8")
        if p.returncode: raise RuntimeError(f"compilation failed: mode={mode} file={name}")
        elf = subprocess.run([str(readelf), "-h", str(obj)], check=True, capture_output=True, text=True).stdout
        if not re.search(r"Type:\s+REL\b", elf) or not re.search(r"Machine:\s+AArch64\b", elf):
            raise RuntimeError(f"invalid object: mode={mode} file={name}")
        return mode, name, obj, dep

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        results = list(pool.map(compile_one, ((m, n) for m in ("0", "1", "android1") for n in NAMES)))
    outputs = {(m, n): o for m, n, o, _ in results}
    changed = [n for n in NAMES if digest(outputs[("0", n)]) != digest(outputs[("1", n)])]
    changed_android_flags = [n for n in NAMES if digest(outputs[("1", n)]) != digest(outputs[("android1", n)])]
    baseline_mismatch = [n for n in NAMES if digest(outputs[("0", n)]) != baseline[n][1]]
    if baseline_mismatch:
        raise RuntimeError(f"macro=0 does not reproduce previous objects: {baseline_mismatch}")
    if "ResourceTracker" not in changed:
        raise RuntimeError("Goldfish macro did not alter ResourceTracker")
    tracker = guest / "vulkan_enc/ResourceTracker.cpp"
    for mode in (0, 1):
        pp = out / f"{mode}-ResourceTracker.i"
        cmd = common + [f"-DGFXSTREAM_ENABLE_GUEST_GOLDFISH={mode}", "-E", "-P", str(tracker), "-o", str(pp)]
        (out / f"{mode}-preprocess.command.txt").write_text(shlex.join(cmd) + "\n", encoding="utf-8")
        subprocess.run(cmd, check=True)
    pp0 = (out / "0-ResourceTracker.i").read_text(encoding="utf-8")
    pp1 = (out / "1-ResourceTracker.i").read_text(encoding="utf-8")
    if "goldfish_sync_open(" in pp0 or "goldfish_sync_queue_work(" in pp0 or \
       "goldfish_sync_open(" not in pp1 or "goldfish_sync_queue_work(" not in pp1:
        raise RuntimeError("unexpected preprocessed Goldfish sync branch")
    def undefined(obj):
        text = subprocess.run([str(nm), "-u", str(obj)], check=True, capture_output=True, text=True).stdout
        return {line.split()[-1] for line in text.splitlines() if line.split()}
    syms0, syms1 = undefined(outputs[("0", "ResourceTracker")]), undefined(outputs[("1", "ResourceTracker")])
    added, removed = syms1 - syms0, syms0 - syms1
    if not {"open", "ioctl"}.issubset(added): raise RuntimeError("expected Goldfish device calls missing")
    for name, values in (("tracker-added.txt", added), ("tracker-removed.txt", removed)):
        (out / name).write_text("".join(s + "\n" for s in sorted(values)), encoding="utf-8")
    observed = {p.resolve() for p in (cc, nm, readelf, bp, encoder_bp, *[guest / "vulkan_enc" / (n + ".cpp") for n in NAMES])}
    for _, _, _, dep in results:
        for item in shlex.split(dep.read_text(encoding="utf-8").replace("\\\n", " ").split(":", 1)[1]):
            path = pathlib.Path(item)
            if path.is_file(): observed.add(path.resolve())
    (out / "inputs.sha256").write_text("".join(f"{digest(p)}  {p}\n" for p in sorted(observed)), encoding="utf-8")
    (out / "objects.tsv").write_text("".join(f"{m}\t{n}\t{digest(outputs[(m,n)])}\n" for m in ("0", "1", "android1") for n in NAMES), encoding="utf-8")
    (out / "changed-objects.txt").write_text("".join(n + "\n" for n in changed), encoding="utf-8")
    print(f"BUILT_MACRO_0={len(NAMES)} BUILT_MACRO_1={len(NAMES)} BUILT_ANDROID_FLAGS_1={len(NAMES)} BASELINE_MATCH={len(NAMES)}")
    print("CHANGED=" + ",".join(changed))
    print("ANDROID_FLAGS_CHANGED=" + ",".join(changed_android_flags))
    print("TRACKER_ADDED=" + ",".join(sorted(added)))
    print("TRACKER_REMOVED=" + ",".join(sorted(removed)))
    print("PREPROCESS_BRANCH_CHECK_OK (no component executed; no device accessed)")


if __name__ == "__main__":
    main()
