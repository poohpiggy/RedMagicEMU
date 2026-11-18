#!/usr/bin/env python3
"""Compile exact source fragments against CPU-only fence/encoder substitutes."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--cxx", required=True)
parser.add_argument("--patch", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    target = root / "guest/vulkan_enc"
    target.mkdir(parents=True)
    for name in ("ResourceTracker.cpp", "ResourceTracker.h", "VkEncoder.cpp"):
        shutil.copyfile(args.source / "guest/vulkan_enc" / name, target / name)
    if args.patch:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(root),
                        "-i", str(args.patch.resolve())], check=True)
    source = (target / "ResourceTracker.cpp").read_text()
    match = re.search(r"(void|VkResult) ResourceTracker::unwrap_vkAcquireImageANDROID_nativeFenceFd\([^\n]+\) \{", source)
    if not match:
        raise SystemExit("actual helper signature not found")
    start = match.start()
    pos = match.end()
    depth = 1
    while depth and pos < len(source):
        if source[pos] == "{": depth += 1
        if source[pos] == "}": depth -= 1
        pos += 1
    if depth:
        raise SystemExit("actual helper body is incomplete")
    helper = source[start:pos]
    encoder = (target / "VkEncoder.cpp").read_text()
    index = encoder.index("VkResult VkEncoder::vkAcquireImageANDROID(")
    start = encoder.index("sResourceTracker->unwrap_vkAcquireImageANDROID_nativeFenceFd", index)
    start = encoder.rfind("\n", 0, start) + 1
    end = encoder.index("size_t count = 0;", start)
    checkpoint = encoder[start:end]
    template = (here / "harness.cpp").read_text()
    text = template.replace("@RETURN_TYPE@", match.group(1)).replace("@HELPER@", helper).replace("@CHECKPOINT@", checkpoint)
    unit = root / "test.cpp"
    unit.write_text(text)
    binary = root / "test"
    subprocess.run([args.cxx, "-std=c++17", str(unit), "-o", str(binary)], check=True)
    result = subprocess.run([str(binary)])
    if result.returncode:
        raise SystemExit(f"actual helper/acquire checkpoint failed: {result.returncode}")
    print("PASS: fence failure stops encoding, fd closes once, and encoder lock is balanced")
