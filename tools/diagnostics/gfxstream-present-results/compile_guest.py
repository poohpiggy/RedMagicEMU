#!/usr/bin/env python3
"""Compile experimental generated guest code without modifying an existing build."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("generated", type=Path)
    parser.add_argument("encoder_build", type=Path, help="existing d7a encoder output")
    parser.add_argument("source", type=Path, help="d7a source")
    parser.add_argument("output", type=Path, help="must be a new directory")
    args = parser.parse_args()
    generated = args.generated.resolve(strict=True) / "guest"
    old_root = args.encoder_build.resolve(strict=True)
    old = old_root / "patched-source/guest/vulkan_enc"
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    encoder = out / "guest/vulkan_enc"
    shutil.copytree(old, encoder)
    for name in ("egl", "OpenglSystemCommon"):
        (encoder.parent / name).symlink_to((args.source.resolve(strict=True) / "guest" / name),
                                          target_is_directory=True)
    names = ("VkEncoder", "goldfish_vk_counting_guest", "goldfish_vk_deepcopy_guest",
             "goldfish_vk_extension_structs_guest", "goldfish_vk_marshaling_guest",
             "goldfish_vk_reserved_marshaling_guest", "goldfish_vk_transform_guest")
    for name in names:
        for suffix in (".cpp", ".h"):
            candidate = generated / (name + suffix)
            if candidate.exists():
                shutil.copy2(candidate, encoder / candidate.name)
    template = shlex.split((old_root / "android1-VkEncoder.command.txt").read_text())

    def compile_one(name):
        cmd = []
        for arg in template:
            arg = arg.replace(str(old), str(encoder))
            arg = arg.replace(str(old_root / "android1-VkEncoder"), str(out / ("android1-" + name)))
            arg = arg.replace(str(encoder / "VkEncoder.cpp"), str(encoder / (name + ".cpp")))
            cmd.append(arg)
        (out / (name + ".command.txt")).write_text(shlex.join(cmd) + "\n")
        result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (name + ".log")).write_text(result.stdout)
        return name, result.returncode

    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(compile_one, names))
    for name, code in results:
        print(f"{name}: {code}", flush=True)
    if any(code for _, code in results):
        sys.exit(1)


if __name__ == "__main__":
    main()
