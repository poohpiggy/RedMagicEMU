#!/usr/bin/env python3
"""Stage an isolated device probe using the existing linked gfxstream encoder."""
import argparse
from pathlib import Path
import re
import shlex
import struct
import subprocess

HERE = Path(__file__).resolve().parent


def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, timeout=90, **kwargs)


def build(template, client, gles, drm, out):
    out.mkdir()
    command = shlex.split(template.read_text())
    toolbin = Path(command[0]).parent
    # Reuse the headers and ABI flags from the fixed gfxstream encoder build.
    shaders = toolbin.parents[4] / "shader-tools/linux-x86_64"
    definitions = ["#include <cstdint>"]
    for name, symbol in (("transform", "kTransformSpv"), ("count", "kCountSpv")):
        shader = out / (name + ".spv")
        run([shaders / "glslc", "--target-env=vulkan1.1", HERE / ("guest_probe_" + name + ".comp"),
             "-o", shader])
        run([shaders / "spirv-val", "--target-env", "vulkan1.1", shader])
        words = ", ".join(f"0x{word:08x}u" for (word,) in struct.iter_unpack("<I", shader.read_bytes()))
        definitions.append(f"static constexpr uint32_t {symbol}[] = {{ {words} }};")
    (out / "guest_probe_shaders.h").write_text("\n".join(definitions) + "\n")

    objects = []
    for name in ("guest_device_probe", "guest_compute_probe"):
        obj = out / (name + ".o")
        compile_command = command.copy()
        compile_command[1:1] = ["-I", str(out)]
        for flag, path in (("-c", HERE / (name + ".cpp")), ("-MF", obj.with_suffix(".d")), ("-o", obj)):
            compile_command[compile_command.index(flag) + 1] = str(path)
        run(compile_command)
        objects.append(obj)

    readelf = toolbin / "llvm-readelf"
    dynamic = run([readelf, "-d", client], capture_output=True, text=True).stdout
    match = re.search(r"Library soname: \[(.+)\]", dynamic)
    soname = match.group(1) if match else "libRmeVkProbeClient.so"
    runtime = toolbin.parent / "sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
    libraries = {
        soname: client,
        "libRme_gles1_enc.so": gles / "libRme_gles1_enc.so",
        "libRme_gles2_enc.so": gles / "libRme_gles2_enc.so",
        "libRmeCodecCommon.so": gles / "libRmeCodecCommon.so",
        "libdrm.so": drm,
        "libc++_shared.so": runtime,
    }
    stage = out / "guest"
    stage.mkdir()
    for name, library in libraries.items():
        run([toolbin / "llvm-strip", "--strip-debug", "-o", stage / name, library])
    program = stage / "rme-vulkan-device-probe"
    run([command[0], "-fPIE", "-pie", "-Wl,--no-undefined", "-Wl,-z,max-page-size=16384",
         *objects, "-L" + str(stage), "-l:" + soname, "-o", program])
    print("GUEST_DEVICE_PROBE_BUNDLE", stage)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("template", "client", "gles", "drm", "out"):
        parser.add_argument(field, type=Path)
    args = parser.parse_args()
    build(*(getattr(args, name).resolve() for name in ("template", "client", "gles", "drm", "out")))
