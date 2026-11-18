#!/usr/bin/env python3
"""Compile generated host decoder with its own generated cereal headers."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("generated", type=Path)
parser.add_argument("compilation_database", type=Path)
parser.add_argument("output", type=Path, help="must not already exist")
args = parser.parse_args()
generated = args.generated.resolve(strict=True)
entries = json.loads(args.compilation_database.read_text())
matches = [entry for entry in entries if entry["file"].endswith("/VkDecoder.cpp")]
if len(matches) != 1:
    raise SystemExit("expected one VkDecoder.cpp compile command")
entry = matches[0]
cwd = Path(entry["directory"])
original = (cwd / entry["file"]).resolve(strict=True)
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
original_source = original.parent.parent.parent
isolated_source = output / "source"
shutil.copytree(original_source, isolated_source, symlinks=True)
isolated = isolated_source / "host/vulkan"
for name in ("VkDecoder.cpp", "VkDecoder.h", "VkDecoderSnapshot.h"):
    shutil.copy2(generated / "host" / name, isolated / name)
if '#include "host_guest_surface.h"' in (generated / "host/VkDecoder.cpp").read_text():
    shutil.copy2(Path(__file__).with_name("host_guest_surface.h"), isolated / "host_guest_surface.h")
for header in (generated / "host/cereal/common").glob("*.h"):
    shutil.copy2(header, isolated / "cereal/common" / header.name)
command = list(entry.get("arguments") or shlex.split(entry["command"]))
for index, value in enumerate(command):
    if value.startswith("-I../source"):
        command[index] = "-I" + str(isolated_source) + value[len("-I../source"):]
command[1:1] = ["-I" + str(isolated), "-I" + str(isolated / "cereal")]
command[command.index(entry["file"])] = str(isolated / "VkDecoder.cpp")
command[command.index("-o") + 1] = str(output / "VkDecoder.o")
if "-MF" in command:
    command[command.index("-MF") + 1] = str(output / "VkDecoder.d")
(output / "command.txt").write_text(shlex.join(command) + "\n")
with (output / "compile.log").open("w") as log:
    subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
print("ANDROID_SURFACE_DECODER_OBJECT_OK", output / "VkDecoder.o")
