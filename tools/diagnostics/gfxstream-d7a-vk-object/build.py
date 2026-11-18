#!/usr/bin/env python3
"""Compile the real Mesa object runtime and inspect strict link failures."""
import argparse
import hashlib
import pathlib
import re
import shlex
import subprocess
import tempfile

BASE_SHA256 = "e4477501de21dbef459498600796f90c5d953ed32dcd95f22c845d8966beb9e5"
SOURCE_SHA256 = {
    "vk_object": "3b3e0c892f198ced4a45e8bb14d382d3f79f81afa26cdc1856b4c70ea6f54f46",
    "sparse_array": "34f4def81df53617fdc8e7c11622c993c0f6025dbe686ce49b9825d3e6642db1",
    "hash_table": "7214bc24c0dab9db33e1a485b291258a2c7fba391df55a6bd6f57c2f9d501f54",
    "ralloc": "399eb11141df8c47f7985c1d79f15b4312b569daf19da9ddd173bd18a6c5bc5d",
    "u_printf": "69dafe005eaf8805377d28a5aeb4f4c3ad8d46cc43299b28963149171a90a4d5",
    "vk_enum_to_str": "74331f4ef906292217530d82204e367ee89ed5cfc7deb3ea2db3ae07f4104fcf",
}
EXPECTED = {"vk_object_free", "vk_object_zalloc"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def undefined(nm, path):
    p = subprocess.run([str(nm), "-u", str(path)], check=True, capture_output=True, text=True)
    return {line.split()[-1] for line in p.stdout.splitlines() if line.split()}


def main():
    ap = argparse.ArgumentParser()
    for name in ("source", "generated", "aosp", "ndk", "encoder-dir", "memstream-dir"):
        ap.add_argument("--" + name, type=pathlib.Path, required=True)
    a = ap.parse_args()
    src, generated, aosp, ndk, enc, mem = [p.resolve(strict=True) for p in
        (a.source, a.generated, a.aosp, a.ndk, a.encoder_dir, a.memstream_dir)]
    guest, built = src / "guest", generated / "guest"
    bin_dir = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    cc, cxx = bin_dir / "aarch64-linux-android35-clang", bin_dir / "aarch64-linux-android35-clang++"
    ld, nm, readelf = [bin_dir / p for p in ("ld.lld", "llvm-nm", "llvm-readelf")]
    lib = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android"
    libs = (lib / "libc++_shared.so", lib / "35/liblog.so", lib / "35/libandroid.so")
    base = mem / "encoder-with-memstream-relocatable.o"
    sources = {"vk_object": guest / "mesa/src/vulkan/runtime/vk_object.c",
               **{n: guest / "mesa/src/util" / (n + ".c") for n in
                  ("sparse_array", "hash_table", "ralloc", "u_printf")},
               "vk_enum_to_str": built / "mesa/src/vulkan/util/vk_enum_to_str.c"}
    meson = sources["vk_object"].parent / "meson.build"
    for p in (cc, cxx, ld, nm, readelf, base, meson, *sources.values(), *libs):
        if not p.is_file():
            raise FileNotFoundError(p)
    if digest(base) != BASE_SHA256 or any(digest(p) != SOURCE_SHA256[n] for n, p in sources.items()):
        raise RuntimeError("unexpected baseline object or runtime source")
    if "'vk_object.c'" not in meson.read_text(encoding="utf-8"):
        raise RuntimeError("vk_object.c missing from same-source Mesa runtime")
    before = undefined(nm, base)
    if len(before) != 131 or not EXPECTED.issubset(before) or "vk_sync_dummy_type" not in before:
        raise RuntimeError("unexpected baseline symbols")
    overlay = enc / "platform-headers"
    platform = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android"
    for link, target in ((overlay / "android/hardware_buffer.h", platform / "hardware_buffer.h"),
                         (overlay / "android/data_space.h", platform / "data_space.h"),
                         (overlay / "android/rect.h", platform / "rect.h"),
                         (overlay / "vndk/hardware_buffer.h", aosp / "aosp-nativewindow-full/include/vndk/hardware_buffer.h")):
        if not target.is_file() or link.resolve(strict=True) != target.resolve(strict=True):
            raise RuntimeError(f"unexpected platform header: {link}")
    inc = [guest / "vulkan", guest / "vulkan_enc", guest / "mesa/include", guest / "mesa/src",
           guest / "mesa/src/util", guest / "mesa/src/vulkan/runtime", guest / "mesa/src/vulkan/util",
           guest / "OpenglSystemCommon", guest / "OpenglCodecCommon", guest / "OpenglCodecCommon/include",
           guest / "iostream/include", guest / "qemupipe/include", guest / "qemupipe/include-types",
           guest / "android-emu", guest / "android/include", guest / "include", guest / "renderControl_enc",
           guest / "GoldfishAddressSpace/include", guest / "platform/include", guest / "gralloc_cb/include",
           guest / "GLESv1_enc", guest / "GLESv2_enc", built / "vulkan_enc", built / "mesa/src/vulkan/util",
           built / "mesa/src/vulkan/runtime", src / "common/vulkan/include", src / "common/opengl/include",
           aosp / "aosp-liblog-includes", aosp / "aosp-libcutils-includes", aosp / "aosp-libbase-files/include",
           overlay, aosp / "aosp-core/libsystem/include", guest / "mesa/include/android_stub",
           aosp / "src/subprojects/libdrm-2.4.133/include/drm"]
    if not all(p.is_dir() for p in inc):
        raise FileNotFoundError("missing supplied include directory")
    out = pathlib.Path(tempfile.mkdtemp(prefix="rme-vk-object-", dir="/var/tmp"))
    print(f"ARTIFACTS={out}", flush=True)

    def strict(label, obj):
        target = out / (label + ".so")
        cmd = [str(cxx), "-shared", "-nostdlib++", "-Wl,--no-undefined", "-Wl,--error-limit=0",
               "-Wl,-z,max-page-size=16384", str(obj), *map(str, libs), "-o", str(target)]
        (out / (label + ".command.txt")).write_text(shlex.join(cmd) + "\n", encoding="utf-8")
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / (label + ".log")).write_text(p.stdout, encoding="utf-8")
        errors = set(re.findall(r"^ld\.lld: error: undefined symbol: (.+)$", p.stdout, re.M))
        if p.returncode == 0 or target.exists() or not errors:
            raise RuntimeError(f"unexpected {label} link outcome")
        (out / (label + ".undefined.txt")).write_text("".join(x + "\n" for x in sorted(errors)), encoding="utf-8")
        return errors

    red = strict("strict-before", base)
    if not EXPECTED.issubset(red) or "vk_sync_dummy_type" not in red:
        raise RuntimeError("baseline does not reproduce expected missing symbols")
    common = [str(cc), "-std=gnu11", "-fPIC", "-DANDROID", "-DANDROID_API_LEVEL=35",
           "-DVK_USE_PLATFORM_ANDROID_KHR", "-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT", "-DHAVE_STRUCT_TIMESPEC",
           "-DHAVE_PTHREAD", "-DHAVE_THRD_CREATE", "-DVK_NO_NIR", "-Werror=implicit-function-declaration",
           "-Werror=incompatible-pointer-types", f"-ffile-prefix-map={out}=/rme/vk-object-build"]
    common.extend("-I" + str(p) for p in inc)
    objects, deps = [], []
    for name, source in sources.items():
        obj, dep = out / (name + ".o"), out / (name + ".d")
        cmd = common + ["-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)]
        (out / (name + ".command.txt")).write_text(shlex.join(cmd) + "\n", encoding="utf-8")
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / (name + ".log")).write_text(p.stdout, encoding="utf-8")
        if p.returncode:
            raise RuntimeError(f"runtime compilation failed: {name}")
        elf = subprocess.run([str(readelf), "-h", str(obj)], check=True, capture_output=True, text=True).stdout
        if not re.search(r"Type:\s+REL\b", elf) or not re.search(r"Machine:\s+AArch64\b", elf):
            raise RuntimeError(f"runtime object is not AArch64 REL: {name}")
        objects.append(obj)
        deps.append(dep)
        print(f"COMPILED {name} {digest(obj)}", flush=True)
    combined = out / "encoder-with-vk-object-relocatable.o"
    subprocess.run([str(ld), "-r", "-o", str(combined), str(base), *map(str, objects)], check=True)
    after = undefined(nm, combined)
    if not EXPECTED.issubset(before - after) or "vk_sync_dummy_type" not in after:
        raise RuntimeError("runtime symbol result was unexpected")
    green = strict("strict-after", combined)
    if EXPECTED & green or "vk_sync_dummy_type" not in green:
        raise RuntimeError("strict link symbol result was unexpected")
    for name, entries in (("resolved.txt", before - after), ("introduced.txt", after - before),
                          ("strict-resolved.txt", red - green), ("strict-introduced.txt", green - red)):
        (out / name).write_text("".join(x + "\n" for x in sorted(entries)), encoding="utf-8")
    observed = {p.resolve() for p in (cc, cxx, ld, nm, readelf, base, meson, *sources.values(), *libs)}
    for dep in deps:
        for item in shlex.split(dep.read_text(encoding="utf-8").replace("\\\n", " ").split(":", 1)[1]):
            path = pathlib.Path(item)
            if path.is_file(): observed.add(path.resolve())
    (out / "inputs.sha256").write_text("".join(f"{digest(p)}  {p}\n" for p in sorted(observed)), encoding="utf-8")
    print(f"OBJECT_UNDEFINED_BEFORE={len(before)} RESOLVED={len(before-after)} INTRODUCED={len(after-before)} AFTER={len(after)}")
    print(f"STRICT_UNDEFINED_BEFORE={len(red)} RESOLVED={len(red-green)} INTRODUCED={len(green-red)} AFTER={len(green)}")
    (out / "objects.sha256").write_text("".join(f"{digest(obj)}  {obj.name}\n" for obj in objects), encoding="utf-8")
    print(f"RELOCATABLE_SHA256={digest(combined)}")
    print("STRICT_LINK_FAILED (no shared library; no component executed)")


if __name__ == "__main__":
    main()
