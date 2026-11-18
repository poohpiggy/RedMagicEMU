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

def extract(source, signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        if source[pos] == "{": depth += 1
        if source[pos] == "}": depth -= 1
        pos += 1
    return source[start:pos]

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    target = root / "guest/vulkan/gfxstream_vk_device.cpp"
    target.parent.mkdir(parents=True)
    shutil.copyfile(args.source / "guest/vulkan/gfxstream_vk_device.cpp", target)
    if args.patch:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(root), "-i", str(args.patch.resolve())], check=True)
    source = target.read_text()
    harness = (here / "harness.cpp").read_text().replace("@DEVICE_FUNCTION@", extract(source, "static void get_device_extensions("))
    harness = harness.replace("@INSTANCE_FUNCTION@", extract(source, "static struct vk_instance_extension_table* get_instance_extensions()"))
    unit = root / "test.cpp"
    unit.write_text(harness)
    executable = root / "test"
    subprocess.run([args.cxx, "-std=c++17", str(unit), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable)])
    if result.returncode:
        raise SystemExit(f"WSI advertisement gate failed: {result.returncode}")
    print("PASS: incomplete Android surface/swapchain hidden, unrelated extensions preserved")
