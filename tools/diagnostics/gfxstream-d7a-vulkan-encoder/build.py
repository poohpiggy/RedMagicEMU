#!/usr/bin/env python3
"""Cross-compile the fixed gfxstream encoder without touching its source tree."""
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
    ap.add_argument("--source", required=True, type=pathlib.Path)
    ap.add_argument("--generated", required=True, type=pathlib.Path)
    ap.add_argument("--aosp", required=True, type=pathlib.Path)
    ap.add_argument("--ndk", required=True, type=pathlib.Path)
    ap.add_argument("--baseline", action="store_true", help="reproduce old-header failures only")
    ap.add_argument("--jobs", type=int, default=2)
    args = ap.parse_args()
    if args.jobs < 1 or args.jobs > 4:
        ap.error("jobs must be between 1 and 4")
    src, generated, aosp, ndk = [p.resolve(strict=True) for p in
                                  (args.source, args.generated, args.aosp, args.ndk)]
    guest = src / "guest"
    built = generated / "guest"
    bin_dir = ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    compiler = bin_dir / "aarch64-linux-android35-clang++"
    readelf, nm, ld, demangler = [bin_dir / p for p in ("llvm-readelf", "llvm-nm", "ld.lld", "llvm-cxxfilt")]
    hardware = ndk / "toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android/hardware_buffer.h"
    dataspace = hardware.parent / "data_space.h"
    rect = hardware.parent / "rect.h"
    vndk = aosp / "aosp-nativewindow-full/include/vndk/hardware_buffer.h"
    aosp_system = aosp / "aosp-core/libsystem/include"
    stub = guest / "mesa/include/android_stub"
    bp = guest / "vulkan_enc/Android.bp"
    for p in (compiler, readelf, nm, ld, demangler, hardware, dataspace, rect, vndk, aosp_system, stub, bp):
        if not p.exists():
            raise FileNotFoundError(p)
    text = bp.read_text(encoding="utf-8")
    group = re.search(r'filegroup\s*\{\s*name:\s*"vulkan_enc_impl",\s*srcs:\s*\[(.*?)\]', text, re.S)
    if not group or re.findall(r'"([^\"]+)\.cpp"', group.group(1)) != list(NAMES):
        raise RuntimeError("unexpected Android.bp encoder file list")
    out = pathlib.Path(tempfile.mkdtemp(prefix="rme-vk-encoder-unified-", dir="/var/tmp"))
    print(f"ARTIFACTS={out}", flush=True)
    overlay = out / "platform-headers"
    for name, target in (("android/hardware_buffer.h", hardware), ("android/data_space.h", dataspace),
                         ("android/rect.h", rect), ("vndk/hardware_buffer.h", vndk)):
        link = overlay / name
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(target)
    includes = [
        guest / "vulkan", guest / "vulkan_enc", guest / "mesa/include", guest / "mesa/src",
        guest / "mesa/src/util", guest / "mesa/src/vulkan/runtime", guest / "mesa/src/vulkan/util",
        guest / "OpenglSystemCommon", guest / "OpenglCodecCommon", guest / "OpenglCodecCommon/include",
        guest / "iostream/include", guest / "qemupipe/include", guest / "qemupipe/include-types",
        guest / "android-emu", guest / "android/include", guest / "include", guest / "renderControl_enc",
        guest / "GoldfishAddressSpace/include", guest / "platform/include", guest / "gralloc_cb/include",
        guest / "GLESv1_enc", guest / "GLESv2_enc", built / "vulkan_enc",
        built / "mesa/src/vulkan/util", built / "mesa/src/vulkan/runtime",
        src / "common/vulkan/include", src / "common/opengl/include", aosp / "aosp-liblog-includes",
        aosp / "aosp-libcutils-includes", aosp / "aosp-libbase-files/include",
    ]
    if not args.baseline:
        includes.append(overlay)
    if not args.baseline:
        includes.append(aosp_system)
    includes.extend((stub, aosp / "src/subprojects/libdrm-2.4.133/include/drm"))
    common = [str(compiler), "-std=gnu++20", "-fPIC", "-DANDROID", "-DANDROID_API_LEVEL=35",
              "-DVK_USE_PLATFORM_ANDROID_KHR", "-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT",
              "-DHAVE_STRUCT_TIMESPEC", "-DHAVE_PTHREAD", "-DHAVE_THRD_CREATE",
              f"-ffile-prefix-map={out}=/rme/encoder-build"]
    common.extend("-I" + str(p) for p in includes)
    names = ("ResourceTracker", "AndroidHardwareBuffer") if args.baseline else NAMES
    def compile_one(name):
        source = guest / "vulkan_enc" / (name + ".cpp")
        obj, dep, log = (out / (name + suffix) for suffix in (".o", ".d", ".log"))
        cmd = common + ["-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)]
        (out / (name + ".command.txt")).write_text(shlex.join(cmd) + "\n", encoding="utf-8")
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.write_text(p.stdout, encoding="utf-8")
        if args.baseline:
            if p.returncode == 0 or "AHARDWAREBUFFER_FORMAT_R8_UNORM" not in p.stdout:
                raise RuntimeError(f"baseline {name} did not reproduce expected failure; see {log}")
            return f"BASELINE_EXPECTED_FAILURE {name}"
        if p.returncode:
            raise RuntimeError(f"{name} compilation failed; see {log}")
        elf = subprocess.run([str(readelf), "-h", str(obj)], check=True, capture_output=True, text=True).stdout
        if not re.search(r"Type:\s+REL\b", elf) or not re.search(r"Machine:\s+AArch64\b", elf):
            raise RuntimeError(f"{name} is not AArch64 relocatable ELF")
        return f"COMPILED {name}"
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for line in pool.map(compile_one, names):
            print(line, flush=True)
    if args.baseline:
        print("BASELINE_CONFIRMED (old Mesa hardware_buffer header lacks R8_UNORM)")
        return
    stub_headers, selected_hardware, all_headers = set(), set(), set()
    manifest = []
    for name in names:
        source = guest / "vulkan_enc" / (name + ".cpp")
        obj = out / (name + ".o")
        dep_text = (out / (name + ".d")).read_text(encoding="utf-8").replace("\\\n", " ")
        deps = shlex.split(dep_text.split(":", 1)[1])
        for item in deps:
            p = pathlib.Path(item)
            if p.is_file():
                all_headers.add(p.resolve())
            try:
                rel = p.relative_to(stub)
                stub_headers.add(rel.as_posix())
            except ValueError:
                pass
            if p.name == "hardware_buffer.h" and p.parent.name in ("android", "vndk"):
                selected_hardware.add(str(p))
                if p.resolve() not in (hardware.resolve(), vndk.resolve()):
                    raise RuntimeError(f"unexpected hardware_buffer header: {p}")
        manifest.append(f"{name}\tsource_sha256={digest(source)}\tobject_sha256={digest(obj)}")
    if not any(pathlib.Path(p).resolve() == hardware.resolve() for p in selected_hardware):
        raise RuntimeError("NDK hardware_buffer header was not observed in dependencies")
    if not any(pathlib.Path(p).resolve() == vndk.resolve() for p in selected_hardware):
        raise RuntimeError("AOSP VNDK hardware_buffer header was not observed in dependencies")
    if any(name in stub_headers for name in ("android/hardware_buffer.h", "android/data_space.h", "android/rect.h", "vndk/hardware_buffer.h")):
        raise RuntimeError("an overridden NDK/AOSP header was shadowed by a Mesa stub")
    if any(name.startswith("system/graphics") for name in stub_headers):
        raise RuntimeError("an AOSP graphics header was shadowed by a Mesa stub")
    (out / "objects.tsv").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    (out / "mesa-stub-headers.txt").write_text("\n".join(sorted(stub_headers)) + "\n", encoding="utf-8")
    (out / "hardware-buffer-headers.txt").write_text("\n".join(sorted(selected_hardware)) + "\n", encoding="utf-8")
    input_paths = all_headers | {compiler.resolve(), bp.resolve()}
    (out / "inputs.sha256").write_text("".join(f"{digest(p)}  {p}\n" for p in sorted(input_paths)), encoding="utf-8")
    objects = [str(out / (name + ".o")) for name in names]
    combined = out / "encoder-relocatable.o"
    subprocess.run([str(ld), "-r", "-o", str(combined), *objects], check=True)
    undefined = subprocess.run([str(nm), "-u", str(combined)], check=True, capture_output=True, text=True).stdout
    (out / "undefined-symbols.txt").write_text(undefined, encoding="utf-8")
    raw = sorted({line.split()[-1] for line in undefined.splitlines() if line.split()})
    demangled = subprocess.run([str(demangler)], input="\n".join(raw) + "\n", check=True,
                               capture_output=True, text=True).stdout.splitlines()
    if len(raw) != len(demangled):
        raise RuntimeError("could not demangle every undefined symbol")
    common_c = {"abort", "atoi", "fprintf", "free", "lseek", "malloc", "memcmp", "memcpy",
                "memmove", "memset", "posix_memalign", "read", "realloc", "snprintf", "stderr",
                "strcmp", "strerror", "strlen", "syscall", "usleep", "write", "__assert2", "__dso_handle", "__errno"}
    counts = {"project": 0, "runtime": 0, "platform": 0, "other": 0}
    rows = []
    for symbol, readable in zip(raw, demangled):
        if readable.startswith(("VirtGpu", "Goldfish", "gfxstream::", "mesa_", "vk_")):
            kind = "project"
        elif readable.startswith(("std::", "typeinfo for std::", "vtable for std::", "vtable for __cxxabiv1", "operator new", "operator delete", "__cxa", "__aarch64", "__gxx", "_Unwind", "pthread_")) or readable in common_c:
            kind = "runtime"
        elif readable.startswith(("__android_", "property_")):
            kind = "platform"
        else:
            kind = "other"
        counts[kind] += 1
        rows.append(f"{kind}\t{symbol}\t{readable}")
    (out / "undefined-classified.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"OBJECTS={len(names)} STUB_HEADERS={len(stub_headers)} UNDEFINED_LINES={len(undefined.splitlines())} UNIQUE_UNDEFINED={len(raw)}")
    print("CLASSIFIED " + " ".join(f"{k}={v}" for k, v in counts.items()))
    print("AARCH64_RELOCATABLE_ONLY (not a shared library; not linked or run)")

if __name__ == "__main__":
    main()
