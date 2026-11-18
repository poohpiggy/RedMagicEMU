#!/usr/bin/env python3
"""Test the actual fixed-version QEMU pipe functions with controlled fake I/O."""
import argparse
import hashlib
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--patch", type=pathlib.Path)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--compiler-arg", action="append", default=[])
    args = parser.parse_args()
    root = args.source.resolve(strict=True)
    guest = root / "guest/qemupipe"
    actual = guest / "qemu_pipe_common.cpp"
    here = pathlib.Path(__file__).resolve().parent
    if not actual.is_file():
        raise FileNotFoundError(actual)
    expected = {
        actual: "70835e3f306d15a9fe7520999449fc0862766e47acb4a7eab3bb369ba3e63a79",
        guest / "include/qemu_pipe_bp.h": "1dcad14b719e54f1eb6bece6a11cd87ad6f4c2fb69b70ba072e68091d4c130b8",
        guest / "include-types/qemu_pipe_types_bp.h": "d15abb665076ba7f4e9dac867b16ab4bbcd53a061dee18c1471136fd85b4bd25",
    }
    for path, want in expected.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != want:
            raise RuntimeError(f"unexpected QEMU pipe input: {path}")
    with tempfile.TemporaryDirectory(prefix="rme-qemupipe-eof-") as d:
        tmp = pathlib.Path(d)
        source = tmp / "guest/qemupipe/qemu_pipe_common.cpp"
        source.parent.mkdir(parents=True)
        source.write_bytes(actual.read_bytes())
        if args.patch:
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(args.patch.resolve())],
                           cwd=tmp, check=True)
        exe = tmp / "test"
        subprocess.run([args.cxx, "-std=gnu++17", "-Wall", "-Wextra", "-Werror", "-O0",
                        "-I", str(guest / "include"), "-I", str(guest / "include-types"),
                        *args.compiler_arg, str(source), str(here / "harness.cpp"), "-o", str(exe)], check=True)
        failures = []
        for op in ("read", "write"):
            for case in ("eof", "partial", "fragment", "retry", "again", "error", "empty"):
                name = op + "-" + case
                try:
                    run = subprocess.run([str(exe), name], capture_output=True, text=True, timeout=1)
                except subprocess.TimeoutExpired:
                    failures.append(name + ": no progress / timeout")
                    continue
                if run.returncode:
                    failures.append(name + f": exit={run.returncode} " + run.stderr.strip())
        if failures:
            print("FAIL actual qemupipe functions:\n" + "\n".join(failures))
            return 1
        print("PASS actual qemupipe functions: 14 fake-I/O cases; no Guest or device")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
