#!/usr/bin/env python3
"""Compile real d7a MemStream and inspect strict Android link failures only."""
import argparse
import hashlib
import pathlib
import re
import shlex
import subprocess
import tempfile

BASE_SHA256 = "0bbf21e731f1c9a2bfdaf7c2690e68dd6ac0d453b354ac37c2918225a010cf4c"
SOURCE_SHA256 = "91ca969345817010c80612d6e95a543bd4a805e92e4fcd0d3037ae2a07250513"
MEMSTREAM_SYMBOLS = {
    "_ZN9gfxstream5guest9MemStream4loadEPNS0_6StreamE",
    "_ZNK9gfxstream5guest9MemStream4saveEPNS0_6StreamE",
}
MEMSTREAM_ERRORS = {
    "gfxstream::guest::MemStream::load(gfxstream::guest::Stream*)",
    "gfxstream::guest::MemStream::save(gfxstream::guest::Stream*) const",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def symbols(nm, path):
    result = subprocess.run([str(nm), "-u", str(path)], check=True,
                            capture_output=True, text=True)
    return {line.split()[-1] for line in result.stdout.splitlines() if line.split()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=pathlib.Path, required=True)
    ap.add_argument("--generated", type=pathlib.Path, required=True)
    ap.add_argument("--aosp", type=pathlib.Path, required=True)
    ap.add_argument("--ndk", type=pathlib.Path, required=True)
    ap.add_argument("--encoder-dir", type=pathlib.Path, required=True)
    ap.add_argument("--base-dir", type=pathlib.Path, required=True)
    args = ap.parse_args()
    src, generated, aosp, ndk, encoder_dir, base_dir = [p.resolve(strict=True) for p in
        (args.source, args.generated, args.aosp, args.ndk, args.encoder_dir, args.base_dir)]
    guest, built = src / "guest", generated / "guest"
    base = guest / "android-emu"
    bin_dir = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    compiler = bin_dir / "aarch64-linux-android35-clang++"
    ld, nm, readelf, demangler = [bin_dir / n for n in
                                  ("ld.lld", "llvm-nm", "llvm-readelf", "llvm-cxxfilt")]
    libs = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android"
    real_libs = (libs / "libc++_shared.so", libs / "35/liblog.so", libs / "35/libandroid.so")
    original = base_dir / "encoder-with-base-relocatable.o"
    source = base / "aemu/base/files/MemStream.cpp"
    bp = base / "Android.bp"
    for path in (compiler, ld, nm, readelf, demangler, original, source, bp, *real_libs):
        if not path.is_file():
            raise FileNotFoundError(path)
    if digest(original) != BASE_SHA256 or digest(source) != SOURCE_SHA256:
        raise RuntimeError("unexpected original object or MemStream source")
    if '"aemu/base/files/MemStream.cpp"' not in bp.read_text(encoding="utf-8"):
        raise RuntimeError("MemStream is not listed in the supplied Android.bp")
    before = symbols(nm, original)
    if len(before) != 133 or not MEMSTREAM_SYMBOLS.issubset(before):
        raise RuntimeError("unexpected baseline: MemStream load/save must both be undefined")

    overlay = encoder_dir / "platform-headers"
    android = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android"
    expected_headers = {
        overlay / "android/hardware_buffer.h": android / "hardware_buffer.h",
        overlay / "android/data_space.h": android / "data_space.h",
        overlay / "android/rect.h": android / "rect.h",
        overlay / "vndk/hardware_buffer.h": aosp / "aosp-nativewindow-full/include/vndk/hardware_buffer.h",
    }
    for link, target in expected_headers.items():
        if not target.is_file() or link.resolve(strict=True) != target.resolve(strict=True):
            raise RuntimeError(f"unexpected platform header: {link}")
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
    out = pathlib.Path(tempfile.mkdtemp(prefix="rme-vk-memstream-", dir="/var/tmp"))
    print(f"ARTIFACTS={out}", flush=True)

    def strict_link(label, obj):
        shared = out / (label + ".so")
        command = [str(compiler), "-shared", "-nostdlib++", "-Wl,--no-undefined",
                   "-Wl,--error-limit=0", "-Wl,-z,max-page-size=16384", str(obj),
                   *map(str, real_libs), "-o", str(shared)]
        (out / (label + ".command.txt")).write_text(shlex.join(command) + "\n", encoding="utf-8")
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (out / (label + ".log")).write_text(proc.stdout, encoding="utf-8")
        errors = set(re.findall(r"^ld\.lld: error: undefined symbol: (.+)$", proc.stdout, re.M))
        if proc.returncode == 0 or shared.exists() or not errors:
            raise RuntimeError(f"unexpected {label} link outcome; inspect {label}.log")
        (out / (label + ".undefined.txt")).write_text(
            "".join(s + "\n" for s in sorted(errors)), encoding="utf-8")
        return errors

    before_errors = strict_link("strict-before", original)
    if not MEMSTREAM_ERRORS.issubset(before_errors):
        raise RuntimeError("baseline strict link did not report both MemStream errors")
    obj, dep = out / "MemStream.o", out / "MemStream.d"
    command = [str(compiler), "-std=gnu++20", "-fPIC", "-DANDROID", "-DANDROID_API_LEVEL=35",
               "-DVK_USE_PLATFORM_ANDROID_KHR", "-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT",
               "-DHAVE_STRUCT_TIMESPEC", "-DHAVE_PTHREAD", "-DHAVE_THRD_CREATE",
               '-DLOG_TAG="androidemu"', "-Wno-missing-field-initializers", "-fvisibility=default",
               "-fstrict-aliasing", f"-ffile-prefix-map={out}=/rme/memstream-build"]
    command.extend("-I" + str(p) for p in includes)
    command.extend(("-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)))
    (out / "compile.command.txt").write_text(shlex.join(command) + "\n", encoding="utf-8")
    proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (out / "compile.log").write_text(proc.stdout, encoding="utf-8")
    if proc.returncode:
        raise RuntimeError("MemStream compilation failed; inspect compile.log")
    elf = subprocess.run([str(readelf), "-h", str(obj)], check=True, capture_output=True, text=True).stdout
    if not re.search(r"Type:\s+REL\b", elf) or not re.search(r"Machine:\s+AArch64\b", elf):
        raise RuntimeError("MemStream object is not an AArch64 relocatable ELF")
    combined = out / "encoder-with-memstream-relocatable.o"
    subprocess.run([str(ld), "-r", "-o", str(combined), str(original), str(obj)], check=True)
    after = symbols(nm, combined)
    resolved, introduced = before - after, after - before
    if not MEMSTREAM_SYMBOLS.issubset(resolved):
        raise RuntimeError("MemStream load/save did not both resolve")
    after_errors = strict_link("strict-after", combined)
    if MEMSTREAM_ERRORS & after_errors:
        raise RuntimeError("MemStream load/save still fail strict shared-library linking")
    for filename, values in (("resolved.txt", resolved), ("introduced.txt", introduced),
                             ("undefined-after.txt", after)):
        (out / filename).write_text("".join(s + "\n" for s in sorted(values)), encoding="utf-8")
    observed = {compiler.resolve(), ld.resolve(), nm.resolve(), readelf.resolve(), demangler.resolve(),
                original.resolve(), bp.resolve(), source.resolve(), *(p.resolve() for p in real_libs)}
    deps = shlex.split(dep.read_text(encoding="utf-8").replace("\\\n", " ").split(":", 1)[1])
    for item in deps:
        p = pathlib.Path(item)
        if p.is_file():
            observed.add(p.resolve())
    (out / "inputs.sha256").write_text("".join(f"{digest(p)}  {p}\n" for p in sorted(observed)), encoding="utf-8")
    print(f"OBJECT_UNDEFINED_BEFORE={len(before)} RESOLVED={len(resolved)} "
          f"INTRODUCED={len(introduced)} AFTER={len(after)}")
    print(f"STRICT_UNDEFINED_BEFORE={len(before_errors)} AFTER={len(after_errors)}")
    print(f"MEMSTREAM_OBJECT_SHA256={digest(obj)}")
    print(f"RELOCATABLE_SHA256={digest(combined)}")
    print("STRICT_LINK_FAILED (no shared library; component not executed)")


if __name__ == "__main__":
    main()
