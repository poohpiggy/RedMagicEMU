#!/usr/bin/env python3
"""Exercise the actual upstream channel-creation method against a controlled worker."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("cxx", type=Path)
p.add_argument("--patch", type=Path)
a = p.parse_args()
relative = Path("host/RendererImpl.cpp")
here = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="rme-renderer-channel-test-") as tmp:
    root = Path(tmp)
    target = root / relative
    target.parent.mkdir()
    target.write_bytes((a.source / relative).read_bytes())
    if a.patch:
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(a.patch.resolve())], cwd=root, check=True)
    src = target.read_text()
    start = src.index("RenderChannelPtr RendererImpl::createRenderChannel(")
    end = src.index("\nvoid RendererImpl::addListener", start)
    (root / "method.h").write_text(src[start:end])
    harness = (here.parent / "gfxstream-render-channel-stop/harness.cpp").read_text()
    harness = harness.replace("  std::lock_guard<std::mutex> lock;", "  std::unique_lock<std::mutex> lock;")
    harness = harness.replace("  explicit AutoLock(std::mutex& m) : lock(m) {}", "  explicit AutoLock(std::mutex& m) : lock(m) {}\n  void unlock() { lock.unlock(); }")
    (root / "test.cpp").write_text(harness)
    subprocess.run([str(a.cxx), "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", str(root / "test.cpp"), "-o", str(root / "test")], check=True)
    for case in ("stopped", "normal", "race"):
        try:
            result = subprocess.run([str(root / "test"), case], timeout=2)
        except subprocess.TimeoutExpired:
            print(f"FAIL: {case} deadlocks during channel shutdown", flush=True)
            raise SystemExit(1)
        if result.returncode:
            print(f"FAIL: {case} returned {result.returncode}", flush=True)
            raise SystemExit(1)
        print(f"PASS: {case}", flush=True)
