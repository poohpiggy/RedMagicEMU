#!/usr/bin/env python3
"""Compile an isolated fixed RendererImpl using the Vulkan-only build database."""
import json
from pathlib import Path
import shlex
import subprocess
import sys

source, database, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=False)
entries = [e for e in json.loads(database.read_text()) if e["file"].endswith("/RendererImpl.cpp")]
if len(entries) != 1:
    raise ValueError("expected exactly one RendererImpl compile command")
entry = entries[0]
cwd = Path(entry["directory"])
original = (cwd / entry["file"]).resolve(strict=True)
target = output / "host/RendererImpl.cpp"
target.parent.mkdir(parents=True)
target.write_bytes((source / "host/RendererImpl.cpp").read_bytes())
(output / "host/RendererImpl.h").write_bytes((source / "host/RendererImpl.h").read_bytes())
(output / "host/RenderChannelImpl.h").write_bytes((source / "host/RenderChannelImpl.h").read_bytes())
for name in ("fix-create.patch", "add-drain.patch", "add-drain-declaration.patch"):
    patch = Path(__file__).with_name(name).resolve()
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=output, check=True)
command = list(entry.get("arguments") or shlex.split(entry["command"]))
command[1:1] = ["-I" + str(original.parent)]
command[command.index(entry["file"])] = str(target.resolve())
command[command.index("-o") + 1] = str((output / "RendererImpl.o").resolve())
if "-MF" in command:
    command[command.index("-MF") + 1] = str((output / "RendererImpl.d").resolve())
(output / "command.txt").write_text(shlex.join(command) + "\n")
with (output / "compile.log").open("w") as log:
    subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
print("PASS: Android ARM64 RendererImpl", output / "RendererImpl.o")
