#!/usr/bin/env python3
"""Compile unchanged AOSP libbase property implementation in the client ABI."""
from pathlib import Path
import shlex
import subprocess
import sys

if len(sys.argv) != 4:
    sys.exit("usage: compile_android_libbase.py TEMPLATE_COMMAND AOSP_LIBBASE OUTPUT")
template, libbase, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
args = shlex.split(template.read_text())
args = [arg for arg in args if "/android_stub" not in arg and not arg.startswith("-DLOG_TAG=")]
args.insert(1, "-I" + str(libbase / "include"))
# Keep this client-local NDK-ABI implementation from being preempted by the
# platform libbase, whose identically mangled functions can return __1 types.
args.insert(1, "-fvisibility=hidden")
for name in ("properties", "parsebool", "strings", "stringprintf"):
    command = args.copy()
    command[command.index("-c") + 1] = str(libbase / (name + ".cpp"))
    command[command.index("-o") + 1] = str(out / ("aosp_" + name + ".o"))
    command[command.index("-MF") + 1] = str(out / ("aosp_" + name + ".d"))
    subprocess.run(command, check=True)
    if "android_stub/" in (out / ("aosp_" + name + ".d")).read_text():
        sys.exit("unexpected Mesa stub headers")
print("built unchanged AOSP property dependencies; runtime namespace remains unverified")
