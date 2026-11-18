#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile the fixed upstream color_buffer.cpp in isolated temporary copies."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
EXPECTED = "be58350191b92a0f43c4a31cd4e23a17762bdb15c39cc73ee8c6c3a064c65747"


def row_for(build):
    rows = json.loads((build / "compile_commands.json").read_text())
    return next(row for row in rows if row.get("output", "").endswith("/color_buffer.cpp.o"))


def compile_one(row, copied_source, out, label, gles, expect_success):
    work = Path(row["directory"])
    original_root = (work / row["file"]).resolve().parent.parent
    args = shlex.split(row["command"])
    command = []
    i = 0
    macro_count = 0
    while i < len(args):
        arg = args[i]
        if arg in ("-o", "-MF", "-MQ", "-MT"):
            i += 2
            continue
        if arg in ("-MD", "-MMD"):
            i += 1
            continue
        if arg.startswith("-DGFXSTREAM_ENABLE_HOST_GLES="):
            arg = "-DGFXSTREAM_ENABLE_HOST_GLES=" + str(gles)
            macro_count += 1
        elif arg == row["file"]:
            arg = str(copied_source / "host/color_buffer.cpp")
        elif arg.startswith("-I") and len(arg) > 2:
            include = (work / arg[2:]).resolve()
            if include.is_relative_to(original_root):
                arg = "-I" + str(copied_source / include.relative_to(original_root))
        command.append(arg)
        i += 1
    if macro_count != 1:
        raise RuntimeError(f"{label}: unexpected GLES define count {macro_count}")
    command += ["-o", str(out / (label + ".o"))]
    (out / (label + ".command.txt")).write_text(
        "cd " + shlex.quote(str(work)) + "\n" + shlex.join(command) + "\n")
    run = subprocess.run(command, cwd=work, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, timeout=120)
    (out / (label + ".log")).write_text(run.stdout)
    if expect_success:
        if run.returncode:
            raise RuntimeError(f"{label}: compilation failed; see log")
    elif run.returncode != 1 or "mColorBufferGl" not in run.stdout:
        raise RuntimeError(f"{label}: expected missing GL field error; see log")
    print(f"{label}: {'passed' if expect_success else 'expected failure'}", flush=True)


def main():
    if len(sys.argv) != 4:
        print("usage: python3 compile-check.py FIXED_SOURCE LINUX_GLES_BUILD ANDROID_NOGLES_BUILD", file=sys.stderr)
        return 2
    source, linux_build, android_build = map(Path, sys.argv[1:])
    if not all(p.is_absolute() for p in (source, linux_build, android_build)):
        raise ValueError("all paths must be absolute")
    data = (source / "host/color_buffer.cpp").read_bytes()
    if hashlib.sha256(data).hexdigest() != EXPECTED:
        raise ValueError("unexpected upstream color_buffer.cpp hash")
    linux = row_for(linux_build)
    android = row_for(android_build)
    out = Path(tempfile.mkdtemp(prefix="rme-colorbuffer-nogles-", dir="/var/tmp"))
    print("artifacts=" + str(out), flush=True)
    copied = out / "source"
    # Dereference input links so patching the temporary copy cannot alter them.
    shutil.copytree(source, copied, symlinks=False)
    # The fixed source does not contain this header; this is only a header
    # scaffold for compiling the individual object, not a backend implementation.
    compat = copied / "host/gles_compat.h"
    if compat.exists() or compat.is_symlink():
        raise ValueError("unexpected existing gles_compat.h")
    shutil.copyfile(HERE / "gles_compat.h", compat)
    compile_one(linux, copied, out, "baseline-linux-gles0", 0, False)
    compile_one(android, copied, out, "baseline-android-gles0", 0, False)
    compile_one(linux, copied, out, "baseline-linux-gles1", 1, True)
    with (HERE / "getter-guards.patch").open("rb") as patch:
        subprocess.run(["patch", "--batch", "--forward", "-p1", "-d", str(copied)],
                       stdin=patch, check=True, timeout=30)
    compile_one(linux, copied, out, "candidate-linux-gles0", 0, True)
    compile_one(android, copied, out, "candidate-android-gles0", 0, True)
    compile_one(linux, copied, out, "candidate-linux-gles1", 1, True)
    print("COLOR_BUFFER_OBJECTS_OK (compile only)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, StopIteration, subprocess.SubprocessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
