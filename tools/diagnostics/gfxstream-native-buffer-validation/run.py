#!/usr/bin/env python3
"""Test the actual on_vkCreateImage preflight against fake gralloc inputs."""
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
    target = root / "guest/vulkan_enc/ResourceTracker.cpp"
    target.parent.mkdir(parents=True)
    shutil.copyfile(args.source / "guest/vulkan_enc/ResourceTracker.cpp", target)
    if args.patch:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(root), "-i", str(args.patch.resolve())], check=True)
    source = target.read_text()
    start = source.index("VkResult ResourceTracker::on_vkCreateImage(")
    end = source.index("    VkImageCreateInfo localCreateInfo", start)
    checkpoint = source[start:end] + "    (void)enc; return VK_SUCCESS;\n}\n"
    unit = root / "test.cpp"
    unit.write_text((here / "harness.cpp").read_text().replace("@CHECKPOINT@", checkpoint))
    exe = root / "test"
    subprocess.run([args.cxx, "-std=c++17", str(unit), "-o", str(exe)], check=True)
    result = subprocess.run([str(exe)])
    if result.returncode:
        raise SystemExit(f"native buffer preflight failed: {result.returncode}")
    print("PASS: unavailable or zero host handle stops encoding; ordinary images pass")
