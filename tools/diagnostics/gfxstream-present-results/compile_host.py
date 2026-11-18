#!/usr/bin/env python3
"""Cross-compile isolated generated decoder using an existing Android build command."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("generated", type=Path)
    parser.add_argument("compilation_database", type=Path)
    parser.add_argument("output", type=Path, help="must be a new directory")
    args = parser.parse_args()
    entries = json.loads(args.compilation_database.read_text())
    entries = [x for x in entries if x["file"].endswith("/VkDecoder.cpp")]
    if len(entries) != 1:
        raise ValueError(f"expected one VkDecoder.cpp command, got {len(entries)}")
    entry = entries[0]
    cwd = Path(entry["directory"])
    original = (cwd / entry["file"]).resolve(strict=True)
    generated = (args.generated / "host/VkDecoder.cpp").resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = entry.get("arguments") or shlex.split(entry["command"])
    command = list(command)
    command[1:1] = ["-I" + str(original.parent)]
    command[command.index(entry["file"])] = str(generated)
    command[command.index("-o") + 1] = str(output / "VkDecoder.o")
    if "-MF" in command:
        command[command.index("-MF") + 1] = str(output / "VkDecoder.d")
    (output / "command.txt").write_text(shlex.join(command) + "\n")
    with (output / "compile.log").open("w") as log:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
    print("PASS: Android ARM64 VkDecoder.o", output / "VkDecoder.o")


if __name__ == "__main__":
    main()
