#!/usr/bin/env python3
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--cxx", required=True)
parser.add_argument("--patch", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    target = root / "guest/vulkan/gfxstream_vk_device.cpp"
    target.parent.mkdir(parents=True)
    shutil.copyfile(args.source / "guest/vulkan/gfxstream_vk_device.cpp", target)
    if args.patch:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(root), "-i", str(args.patch.resolve())], check=True)
    source = target.read_text()
    start = source.index("VkResult gfxstream_vk_EnumerateDeviceExtensionProperties(")
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        if source[pos] == "{": depth += 1
        if source[pos] == "}": depth -= 1
        pos += 1
    unit = root / "test.cpp"
    unit.write_text((here / "harness.cpp").read_text().replace("@FUNCTION@", source[start:pos]))
    exe = root / "test"
    subprocess.run([args.cxx, "-std=c++17", str(unit), "-o", str(exe)], check=True)
    result = subprocess.run([str(exe)])
    if result.returncode:
        raise SystemExit(f"native buffer spec version mismatch: {result.returncode}")
    print("PASS: actual enumeration reports native-buffer spec 7 and preserves other extensions")
