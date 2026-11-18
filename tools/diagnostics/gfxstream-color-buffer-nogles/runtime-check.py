#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare two existing GLES-enabled backends with a real Vulkan color buffer."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run_bounded(command, env, log):
    with log.open("wb") as output:
        proc = subprocess.Popen(command, env=env, stdout=output, stderr=subprocess.STDOUT,
                                start_new_session=True)
        try:
            return proc.wait(timeout=45)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait()
            raise RuntimeError(f"timed out; see {log}")


def main():
    if len(sys.argv) != 6:
        print("usage: runtime-check.py LINUX_GLES_BUILD BASELINE_LIBDIR CANDIDATE_LIBDIR DEP_LIBDIR VULKAN_ICD", file=sys.stderr)
        return 2
    build, base, candidate, deps, icd = map(Path, sys.argv[1:])
    if not all(p.is_absolute() for p in (build, base, candidate, deps, icd)):
        raise ValueError("all paths must be absolute")
    libname = "libgfxstream_backend.so.0.1.2"
    for library in (base, candidate):
        if (library / "libgfxstream_backend.so.0").resolve(strict=True) != (library / libname).resolve(strict=True):
            raise ValueError(f"backend SONAME link does not point at {library / libname}")
    old_hash, new_hash = digest(base / libname), digest(candidate / libname)
    if old_hash == new_hash:
        raise ValueError("baseline and candidate libraries are identical")
    if not icd.is_file():
        raise ValueError("missing Vulkan ICD")
    rows = json.loads((build / "compile_commands.json").read_text())
    row = next(r for r in rows if r.get("output", "").endswith("/color_buffer.cpp.o"))
    args = shlex.split(row["command"])
    command = []
    i = 0
    while i < len(args):
        arg = args[i]
        if arg in ("-o", "-MF", "-MQ", "-MT"):
            i += 2
            continue
        if arg in ("-c", "-MD", "-MMD") or arg == row["file"]:
            i += 1
            continue
        command.append(arg)
        i += 1
    if "-DGFXSTREAM_ENABLE_HOST_GLES=1" not in command:
        raise ValueError("Linux compile database is not GLES-enabled")
    out = Path(tempfile.mkdtemp(prefix="rme-colorbuffer-sync-", dir="/var/tmp"))
    print("artifacts=" + str(out), flush=True)
    (out / "inputs.txt").write_text(f"baseline {old_hash} {base / libname}\ncandidate {new_hash} {candidate / libname}\n")
    command += [str(HERE / "framebuffer-sync-test.cpp"), str(base / libname), "-o", str(out / "probe")]
    (out / "compile-command.txt").write_text("cd " + shlex.quote(row["directory"]) + "\n" + shlex.join(command) + "\n")
    with (out / "compile.log").open("wb") as log:
        subprocess.run(command, cwd=row["directory"], stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=120)
    markers = ("PIXEL_ROUNDTRIP_OK 64", "VK_ONLY_SYNC_PRESERVES_PIXELS 64",
               "UNKNOWN_HANDLE_SYNC_REJECTED", "COLOR_BUFFER_CLOSE_RETURNED",
               "FRAMEBUFFER_FINALIZE_RETURNED")
    for label, library in (("baseline", base), ("candidate", candidate)):
        env = os.environ.copy()
        for key in ("VK_DRIVER_FILES", "VK_ADD_DRIVER_FILES", "LD_PRELOAD", "LD_AUDIT"):
            env.pop(key, None)
        env.update(LD_LIBRARY_PATH=f"{library}:{deps}", VK_ICD_FILENAMES=str(icd),
                   MESA_SHADER_CACHE_DISABLE="true", XDG_CACHE_HOME=str(out / (label + "-cache")))
        log = out / (label + ".log")
        status = run_bounded([str(out / "probe")], env, log)
        contents = log.read_text(errors="replace")
        if status != 0 or any(marker not in contents for marker in markers):
            raise RuntimeError(f"{label}: exit={status} or missing marker; see {log}")
        print(label + ": actual Vulkan roundtrip, sync, error and cleanup passed", flush=True)
    print("RUNTIME_COMPARISON_OK (GLES-enabled build, runtime Vulkan-only)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, StopIteration, subprocess.SubprocessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
