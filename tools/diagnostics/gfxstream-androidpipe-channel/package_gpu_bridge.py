#!/usr/bin/env python3
"""Stage the paired GPU bridge and notices for the normal APK source set."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil


def sha256(path):
    with path.open("rb") as contents:
        return hashlib.file_digest(contents, "sha256").hexdigest()


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("renderer", type=Path, help="build_renderer_service.py output")
parser.add_argument("vulkan", type=Path, help="paired Guest Vulkan library directory")
parser.add_argument("opencl", type=Path, help="Guest Android libOpenCL.so")
parser.add_argument("output", type=Path, help="build/generated/rme-gpu-bridge")
parser.add_argument("--native-opencl-host", type=Path, metavar="LIBAEMUCLHOST", help="paired native OpenCL JNI library")
parser.add_argument("--notice", nargs=2, action="append", required=True, metavar=("NAME", "FILE"))
args = parser.parse_args()

host = args.renderer / "jniLibs/arm64-v8a"
host_manifest = json.loads((args.renderer / "sha256.json").read_text())
assert {"libgfxstream_backend.so", "libaemuvkrenderer.so"} <= host_manifest.keys()
for name, digest in host_manifest.items():
    if sha256(host / name) != digest:
        raise ValueError("renderer hash mismatch for " + name)
host_libraries = {name: host / name for name in host_manifest}
if args.native_opencl_host:
    if args.native_opencl_host.name != "libaemuclhost.so" or args.native_opencl_host.parent != args.opencl.parent:
        parser.error("native OpenCL client and host must be from the same build_android.ps1 output")
    host_libraries["libaemuclhost.so"] = args.native_opencl_host
    for library in (args.opencl, args.native_opencl_host):
        with library.open("rb") as file:
            elf = file.read(20)
        if elf[:6] != b"\x7fELF\x02\x01" or elf[16:20] != b"\x03\x00\xb7\x00":
            parser.error(f"expected an Android ARM64 shared library: {library}")

assets = args.output / "assets/rme-gpu"
jni = args.output / "jniLibs/arm64-v8a"
notices = assets / "licenses"
notices.mkdir(parents=True, exist_ok=True)
jni.mkdir(parents=True, exist_ok=True)
index_file = assets / "guest.idx"
index_file.unlink(missing_ok=True)
native_marker = assets / "native-opencl"
native_marker.unlink(missing_ok=True)
for previous in jni.glob("*.so"):
    if previous.name not in host_libraries:
        previous.unlink()
for name, source in host_libraries.items():
    shutil.copyfile(source, jni / name)
for name, notice in args.notice:
    shutil.copyfile(notice, notices / name)
guest = {name: args.vulkan / name for name in (
    "libRme_gles1_enc.so", "libRme_gles2_enc.so", "libRmeCodecCommon.so", "libc++_shared.so",
)}
guest["libvulkan.so"] = args.vulkan / "libRmePublicVulkan.so"
guest["libOpenCL.so"] = args.opencl
for name, source in sorted(guest.items()):
    shutil.copyfile(source, assets / name)
index = "".join(f"{name}\t{sha256(assets / name)}\t{(assets / name).stat().st_size}\n"
                for name in sorted(guest))
pending_index = assets / "guest.idx.tmp"
pending_index.write_text(index)
if args.native_opencl_host:
    native_marker.write_text("2\n")
pending_index.replace(index_file)
