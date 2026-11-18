#!/usr/bin/env python3
"""Compile the patched d7a global state using an existing Android build command."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("compilation_database", type=Path)
    parser.add_argument("output", type=Path, help="must be a new directory")
    parser.add_argument("--patch", type=Path, action="append", help="apply instead of the default surface patch; may be repeated")
    args = parser.parse_args()
    relative = Path("host/vulkan/VkDecoderGlobalState.cpp")
    entries = json.loads(args.compilation_database.read_text())
    entries = [entry for entry in entries if entry["file"].endswith("/VkDecoderGlobalState.cpp")]
    if len(entries) != 1:
        raise ValueError(f"expected one global-state compile command, got {len(entries)}")
    entry = entries[0]
    cwd = Path(entry["directory"])
    original = (cwd / entry["file"]).resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    target = output / relative
    target.parent.mkdir(parents=True)
    target.write_bytes((args.source / relative).read_bytes())
    patches = [p.resolve(strict=True) for p in args.patch] if args.patch else [Path(__file__).resolve().with_name("fix.patch")]
    for patch in patches:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=output, check=True)
    command = list(entry.get("arguments") or shlex.split(entry["command"]))
    command[1:1] = ["-I" + str(original.parent)]
    command[command.index(entry["file"])] = str(target)
    command[command.index("-o") + 1] = str(output / "VkDecoderGlobalState.o")
    if "-MF" in command:
        command[command.index("-MF") + 1] = str(output / "VkDecoderGlobalState.d")
    (output / "command.txt").write_text(shlex.join(command) + "\n")
    with (output / "compile.log").open("w") as log:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
    print("PASS: Android ARM64 VkDecoderGlobalState.o", output / "VkDecoderGlobalState.o")


if __name__ == "__main__":
    main()
