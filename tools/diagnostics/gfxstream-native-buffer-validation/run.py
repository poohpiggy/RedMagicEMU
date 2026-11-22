#!/usr/bin/env python3
"""Test both native buffer preflights against fake gralloc inputs."""
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
    properties = target.with_name("AndroidHardwareBuffer.cpp")
    shutil.copyfile(args.source / "guest/vulkan_enc/AndroidHardwareBuffer.cpp", properties)
    if args.patch:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-d", str(root), "-i", str(args.patch.resolve())], check=True)
    source = target.read_text()
    start = source.index("VkResult ResourceTracker::on_vkCreateImage(")
    end = source.index("    VkImageCreateInfo localCreateInfo", start)
    checkpoint = source[start:end] + "    (void)enc; return VK_SUCCESS;\n}\n"
    source = properties.read_text()
    start = source.index("VkResult getAndroidHardwareBufferPropertiesANDROID(")
    end = source.index("    const auto format =", start)
    properties_checkpoint = (
        source[start:end] + "    (void)grallocHelper->getFormat(buffer); return VK_SUCCESS;\n}\n"
    )
    unit = root / "test.cpp"
    harness = (here / "harness.cpp").read_text()
    unit.write_text(harness.replace("@CHECKPOINT@", checkpoint).replace(
        "@PROPERTIES_CHECKPOINT@", properties_checkpoint))
    exe = root / "test"
    subprocess.run([args.cxx, "-std=c++17", str(unit), "-o", str(exe)], check=True)
    result = subprocess.run([str(exe)])
    if result.returncode:
        raise SystemExit(f"native buffer preflight failed: {result.returncode}")
    print("PASS: native image creation and AHB properties reject unsupported host handles")
