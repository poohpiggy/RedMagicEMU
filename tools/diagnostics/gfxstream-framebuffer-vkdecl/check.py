#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check one diagnostic in the real source; other Android errors remain."""
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
EXPECTED = "99d35d6469e72b42a301a7e8c501e4f596e224f161617ff013ee787c8a718c55"
SECTION = ".text._ZNK9gfxstream4host11FrameBuffer4Impl14hasEmulationVkEv"


def find_row(build):
    rows = json.loads((build / "compile_commands.json").read_text())
    return next(row for row in rows if row.get("output", "").endswith("/frame_buffer.cpp.o"))


def compile_one(row, source, out, label, android, should_contain):
    cwd = Path(row["directory"])
    original_root = (cwd / row["file"]).resolve().parent.parent
    args = shlex.split(row["command"])
    command = []
    i = 0
    while i < len(args):
        arg = args[i]
        if arg in ("-o", "-MF", "-MQ", "-MT"):
            i += 2
            continue
        if arg in ("-MD", "-MMD"):
            i += 1
            continue
        if arg == row["file"]:
            arg = str(source / "host/frame_buffer.cpp")
        elif arg.startswith("-I") and len(arg) > 2:
            path = (cwd / arg[2:]).resolve()
            if path.is_relative_to(original_root):
                arg = "-I" + str(source / path.relative_to(original_root))
        command.append(arg)
        i += 1
    if android:
        if "-DGFXSTREAM_ENABLE_HOST_GLES=0" not in command:
            raise ValueError("Android build is not GLES=0")
        command.append("-ferror-limit=0")
    elif "-DGFXSTREAM_ENABLE_HOST_GLES=1" not in command:
        raise ValueError("Linux build is not GLES=1")
    obj = out / (label + ".o")
    command += ["-o", str(obj)]
    (out / (label + ".command")).write_text("cd " + shlex.quote(str(cwd)) + "\n" + shlex.join(command) + "\n")
    p = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, timeout=120)
    (out / (label + ".log")).write_text(p.stdout)
    if android:
        marker = "no member named 'hasEmulationVk'"
        if p.returncode != 1 or ((marker in p.stdout) != should_contain):
            raise RuntimeError(f"unexpected {label} diagnostic or status; see log")
        print(f"{label}: getter error {'present' if should_contain else 'absent'}; object still fails", flush=True)
    elif p.returncode:
        raise RuntimeError(f"{label} failed; see log")
    else:
        print(f"{label}: object compiled", flush=True)
    return obj


def main():
    if len(sys.argv) != 4:
        print("usage: check.py SCAFFOLDED_SOURCE ANDROID_BUILD LINUX_GLES_BUILD", file=sys.stderr)
        return 2
    source, android_build, linux_build = map(Path, sys.argv[1:])
    if not all(path.is_absolute() for path in (source, android_build, linux_build)):
        raise ValueError("all paths must be absolute")
    if hashlib.sha256((source / "host/frame_buffer.cpp").read_bytes()).hexdigest() != EXPECTED:
        raise ValueError("unexpected upstream frame_buffer.cpp hash")
    android, linux = find_row(android_build), find_row(linux_build)
    out = Path(tempfile.mkdtemp(prefix="rme-framebuffer-vkdecl-", dir="/var/tmp"))
    print("artifacts=" + str(out), flush=True)
    copied = out / "source"
    shutil.copytree(source, copied, symlinks=False)
    compile_one(android, copied, out, "baseline-android", True, True)
    original_obj = compile_one(linux, copied, out, "baseline-linux", False, False)
    with (HERE / "move-vk-query.patch").open("rb") as patch:
        subprocess.run(["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-d", str(copied)],
                       stdin=patch, check=True, timeout=30)
    compile_one(android, copied, out, "candidate-android", True, False)
    candidate_obj = compile_one(linux, copied, out, "candidate-linux", False, False)
    for label, obj in (("baseline", original_obj), ("candidate", candidate_obj)):
        result = out / (label + "-getter.bin")
        subprocess.run(["objcopy", "--dump-section", f"{SECTION}={result}", str(obj)],
                       check=True, timeout=30)
        relocs = subprocess.check_output(["objdump", "-r", "-j", SECTION, str(obj)], text=True, timeout=30)
        (out / (label + "-relocations.txt")).write_text("\n".join(relocs.splitlines()[2:]) + "\n")
    if ((out / "baseline-getter.bin").read_bytes() != (out / "candidate-getter.bin").read_bytes() or
        (out / "baseline-relocations.txt").read_bytes() != (out / "candidate-relocations.txt").read_bytes()):
        raise RuntimeError("GLES=1 getter code or relocation changed")
    print("GETTER_DIAGNOSTIC_OK (Android object still fails; no device run)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, StopIteration, subprocess.SubprocessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
