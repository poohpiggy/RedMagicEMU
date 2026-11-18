#!/usr/bin/env python3
"""Build three real gfxstream guest base objects and inspect relocatable symbols."""
import argparse
import hashlib
import pathlib
import re
import shlex
import subprocess
import tempfile

ENCODER_SHA256 = "65650c14dce86ec3740c857b933102504cceab44660c06f4a59324da08877c2f"
SOURCE_FILES = (
    "aemu/base/files/Stream.cpp",
    "aemu/base/files/StreamSerializing.cpp",
    "aemu/base/AndroidSubAllocator.cpp",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def undefined(nm, path):
    output = subprocess.run([str(nm), "-u", str(path)], check=True,
                            capture_output=True, text=True).stdout
    return {line.split()[-1] for line in output.splitlines() if line.split()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=pathlib.Path, required=True)
    ap.add_argument("--generated", type=pathlib.Path, required=True)
    ap.add_argument("--aosp", type=pathlib.Path, required=True)
    ap.add_argument("--ndk", type=pathlib.Path, required=True)
    ap.add_argument("--encoder-dir", type=pathlib.Path, required=True)
    args = ap.parse_args()
    src, generated, aosp, ndk, encoder_dir = [p.resolve(strict=True) for p in
        (args.source, args.generated, args.aosp, args.ndk, args.encoder_dir)]
    guest, built = src / "guest", generated / "guest"
    base = guest / "android-emu"
    bin_dir = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    compiler = bin_dir / "aarch64-linux-android35-clang++"
    nm, readelf, ld, demangler = [bin_dir / p for p in
                                 ("llvm-nm", "llvm-readelf", "ld.lld", "llvm-cxxfilt")]
    encoder = encoder_dir / "encoder-relocatable.o"
    for p in (compiler, nm, readelf, ld, demangler, encoder):
        if not p.is_file():
            raise FileNotFoundError(p)
    if digest(encoder) != ENCODER_SHA256:
        raise RuntimeError("unexpected encoder object; rebuild the documented d7a baseline")
    before = undefined(nm, encoder)
    if len(before) != 122:
        raise RuntimeError(f"unexpected baseline undefined symbols: {len(before)}")
    overlay = encoder_dir / "platform-headers"
    platform = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android"
    expected = {
        overlay / "android/hardware_buffer.h": platform / "hardware_buffer.h",
        overlay / "android/data_space.h": platform / "data_space.h",
        overlay / "android/rect.h": platform / "rect.h",
        overlay / "vndk/hardware_buffer.h": aosp / "aosp-nativewindow-full/include/vndk/hardware_buffer.h",
    }
    for link, target in expected.items():
        if not target.is_file() or link.resolve(strict=True) != target.resolve(strict=True):
            raise RuntimeError(f"unexpected documented platform header: {link}")
    includes = [
        guest / "vulkan", guest / "vulkan_enc", guest / "mesa/include", guest / "mesa/src",
        guest / "mesa/src/util", guest / "mesa/src/vulkan/runtime", guest / "mesa/src/vulkan/util",
        guest / "OpenglSystemCommon", guest / "OpenglCodecCommon", guest / "OpenglCodecCommon/include",
        guest / "iostream/include", guest / "qemupipe/include", guest / "qemupipe/include-types",
        base, guest / "android/include", guest / "include", guest / "renderControl_enc",
        guest / "GoldfishAddressSpace/include", guest / "platform/include", guest / "gralloc_cb/include",
        guest / "GLESv1_enc", guest / "GLESv2_enc", built / "vulkan_enc",
        built / "mesa/src/vulkan/util", built / "mesa/src/vulkan/runtime",
        src / "common/vulkan/include", src / "common/opengl/include", aosp / "aosp-liblog-includes",
        aosp / "aosp-libcutils-includes", aosp / "aosp-libbase-files/include", overlay,
        aosp / "aosp-core/libsystem/include", guest / "mesa/include/android_stub",
        aosp / "src/subprojects/libdrm-2.4.133/include/drm",
    ]
    for path in includes:
        if not path.is_dir():
            raise FileNotFoundError(path)
    bp = base / "Android.bp"
    bp_text = bp.read_text(encoding="utf-8")
    for name in SOURCE_FILES:
        if f'"{name}"' not in bp_text:
            raise RuntimeError(f"source not listed by Android.bp: {name}")
    out = pathlib.Path(tempfile.mkdtemp(prefix="rme-vk-base-link-", dir="/var/tmp"))
    print(f"ARTIFACTS={out}", flush=True)
    common = [str(compiler), "-std=gnu++20", "-fPIC", "-DANDROID", "-DANDROID_API_LEVEL=35",
              "-DVK_USE_PLATFORM_ANDROID_KHR", "-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT",
              "-DHAVE_STRUCT_TIMESPEC", "-DHAVE_PTHREAD", "-DHAVE_THRD_CREATE",
              '-DLOG_TAG="androidemu"', "-Wno-missing-field-initializers",
              "-fvisibility=default", "-fstrict-aliasing",
              f"-ffile-prefix-map={out}=/rme/base-build"]
    common.extend("-I" + str(p) for p in includes)
    objs, inputs, stubs = [], {compiler.resolve(), encoder.resolve(), bp.resolve()}, set()
    stub_dir = guest / "mesa/include/android_stub"
    for relative in SOURCE_FILES:
        source = base / relative
        name = source.stem
        obj, dep, log = (out / (name + ext) for ext in (".o", ".d", ".log"))
        cmd = common + ["-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)]
        (out / (name + ".command.txt")).write_text(shlex.join(cmd) + "\n", encoding="utf-8")
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.write_text(proc.stdout, encoding="utf-8")
        if proc.returncode:
            raise RuntimeError(f"compilation failed for {relative}; see {log}")
        elf = subprocess.run([str(readelf), "-h", str(obj)], check=True,
                             capture_output=True, text=True).stdout
        if not re.search(r"Type:\s+REL\b", elf) or not re.search(r"Machine:\s+AArch64\b", elf):
            raise RuntimeError(f"{name} is not an AArch64 relocatable object")
        deps = shlex.split(dep.read_text(encoding="utf-8").replace("\\\n", " ").split(":", 1)[1])
        for item in deps:
            p = pathlib.Path(item)
            if p.is_file():
                inputs.add(p.resolve())
            try:
                stubs.add(p.relative_to(stub_dir).as_posix())
            except ValueError:
                pass
        objs.append(obj)
        print(f"COMPILED {name} {digest(obj)}", flush=True)
    combined = out / "encoder-with-base-relocatable.o"
    subprocess.run([str(ld), "-r", "-o", str(combined), str(encoder), *map(str, objs)], check=True)
    after = undefined(nm, combined)
    resolved, introduced = sorted(before - after), sorted(after - before)
    if not resolved:
        raise RuntimeError("none of the baseline undefined symbols resolved")
    demangled = subprocess.run([str(demangler)], input="\n".join(resolved) + "\n", check=True,
                               capture_output=True, text=True).stdout.splitlines()
    if len(demangled) != len(resolved):
        raise RuntimeError("could not demangle all resolved symbols")
    (out / "resolved.tsv").write_text("".join(f"{s}\t{d}\n" for s, d in zip(resolved, demangled)), encoding="utf-8")
    (out / "introduced.txt").write_text("".join(s + "\n" for s in introduced), encoding="utf-8")
    (out / "undefined-after.txt").write_text("".join(s + "\n" for s in sorted(after)), encoding="utf-8")
    (out / "mesa-stub-headers.txt").write_text("".join(s + "\n" for s in sorted(stubs)), encoding="utf-8")
    (out / "inputs.sha256").write_text("".join(f"{digest(p)}  {p}\n" for p in sorted(inputs)), encoding="utf-8")
    print(f"BEFORE={len(before)} RESOLVED={len(resolved)} INTRODUCED={len(introduced)} AFTER={len(after)}")
    print(f"RELOCATABLE_SHA256={digest(combined)}")
    print("RELOCATABLE_ONLY (not a shared library; no Guest, GPU, or component execution)")


if __name__ == "__main__":
    main()
