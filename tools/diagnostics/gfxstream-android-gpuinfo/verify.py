#!/usr/bin/env python3
"""Verify synthetic enumeration and compile provider using the Android build's flags."""
import argparse
import hashlib
import json
import pathlib
import shlex
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
EXPECTED_HEADER = "daef5d026121976856af82b3a671b345f2ce9a3d2dc5e3ce959798b8a459b15c"

def run(args, cwd=None):
    print("+", shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("gfxstream_root", type=pathlib.Path)
    parser.add_argument("composer_build", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    root, build, out = (p.resolve() for p in (args.gfxstream_root, args.composer_build, args.output))
    entries = json.loads((build / "compile_commands.json").read_text())
    entry = next(x for x in entries if x["file"].endswith("/opengl/gpuinfo.cpp"))
    header = pathlib.Path(entry["directory"]) / entry["file"]
    header = header.resolve().parents[1] / "include/host-common/opengl/gpuinfo.h"
    for path in (root / "src/host/gl/gl-host-common/include/host-common/opengl/gpuinfo.h", header):
        if hashlib.sha256(path.read_bytes()).hexdigest() != EXPECTED_HEADER:
            raise RuntimeError(f"unexpected upstream gpuinfo.h: {path}")
    out.mkdir(parents=True, exist_ok=True)
    executable = out / "gpuinfo_test"
    run([root / "bin/cxx", "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-isystem", root / "src/common/vulkan/include",
         "-isystem", root / "src/host/gl/gl-host-common/include",
         "-isystem", root / "prefix/include", HERE / "query.cpp", HERE / "android_loader.cpp",
         HERE / "query_test.cpp", "-ldl", "-pthread", "-o", executable])
    run([executable])
    command = shlex.split(entry["command"])
    base = []
    i = 0
    while i < len(command):
        if command[i] in ("-o", "-MF", "-MQ", "-c"):
            i += 2
        elif command[i] == "-MD":
            i += 1
        else:
            base.append(command[i]); i += 1
    for source in ("query.cpp", "android_loader.cpp"):
        run(base + ["-c", HERE / source, "-o", out / (source + ".o")], cwd=entry["directory"])
    toolbin = pathlib.Path(command[0]).parent
    run([toolbin / "llvm-readelf", "-h", out / "query.cpp.o", out / "android_loader.cpp.o"])
    nm = subprocess.check_output([str(toolbin / "llvm-nm"), "-C", str(out / "android_loader.cpp.o")], text=True)
    if " T getGpuInfoListNative(GpuInfoList*)" not in nm:
        raise RuntimeError("native provider symbol missing")
    print("ANDROID_ARM64_PROVIDER_OBJECTS_OK; SAFETY_PROVIDER_NOT_IMPLEMENTED")

if __name__ == "__main__":
    main()
