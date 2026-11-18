#!/usr/bin/env python3
"""Check the existing offline build evidence and recompile one object."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

EXPECTED_HEADER = "af0277ffd3756c4c1047071742c362ef1b3f036552eefad98980266fc4660e9c"
EXPECTED_FAILURES = {"getGpuInfoListNative(GpuInfoList*)", "isVulkanSafeToUseNative()"}


def compile_one(entry, output, header_dir=None):
    argv = shlex.split(entry["command"])
    for option in ("-o", "-MF", "-MQ"):
        index = argv.index(option)
        argv[index + 1] = str(output) + (".d" if option == "-MF" else "")
    if header_dir:
        argv[1:1] = ["-I", str(header_dir)]
    return subprocess.run(argv, cwd=entry["directory"], capture_output=True, text=True, timeout=120)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--header-dir", type=Path, required=True)
    args = parser.parse_args()
    baseline, candidate, headers = (args.baseline.resolve(), args.candidate.resolve(), args.header_dir.resolve())
    if hashlib.sha256((headers / "cutils/native_handle.h").read_bytes()).hexdigest() != EXPECTED_HEADER:
        raise RuntimeError("AOSP header SHA-256 mismatch")
    old = json.loads((baseline / "build/compile_commands.json").read_text())
    new = json.loads((candidate / "build-composer/compile_commands.json").read_text())
    if [x for x in old if "NativeSubWindow_" in x["file"]]:
        raise AssertionError("baseline unexpectedly includes native-window source")
    window = [x["file"] for x in new if "NativeSubWindow_" in x["file"]]
    if len(window) != 1 or not window[0].endswith("/NativeSubWindow_android.cpp"):
        raise AssertionError("candidate does not select the Android native-window source")
    print("NATIVE_WINDOW_SELECTION_OK", flush=True)
    matches = [e for e in old if e["file"].endswith("/FrameBuffer.cpp")]
    if len(matches) != 1 or "-DANDROID" not in matches[0]["command"]:
        raise AssertionError("baseline FrameBuffer command is missing or lacks Android definition")
    with tempfile.TemporaryDirectory(prefix="rme-d7a-cutils-check-") as tmp:
        baseline_result = compile_one(matches[0], Path(tmp) / "baseline.o")
        if baseline_result.returncode == 0 or "cutils/native_handle.h" not in baseline_result.stderr:
            raise AssertionError("missing AOSP header was not reproduced: " + baseline_result.stderr[-3000:])
        changed = compile_one(matches[0], Path(tmp) / "candidate.o", headers)
        if changed.returncode:
            raise AssertionError("FrameBuffer did not compile with the AOSP header: " + changed.stderr[-3000:])
        print("AOSP_HEADER_OBJECT_OK", flush=True)
    log = (candidate / "build-composer.log").read_text(errors="replace")
    failures = set(re.findall(r"undefined symbol: (.+)", log))
    required = ("--no-undefined", "-landroid", "NativeSubWindow_android.cpp.o",
                "FrameBuffer.cpp.o", "host/renderControl_dec/libcomposer.a")
    if failures != EXPECTED_FAILURES or any(s not in log for s in required):
        raise AssertionError("recorded link outcome or inputs differ: " + repr(failures))
    print("STRICT_LINK_EXPECTED_FAILURE: " + ", ".join(sorted(failures)))


if __name__ == "__main__":
    main()
