#!/usr/bin/env python3
"""Compile and exercise the actual gfxstream macro with explicit fake dependencies."""
import argparse
import pathlib
import resource
import subprocess
import tempfile


def extract_macro(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    matches = [i for i, line in enumerate(lines) if line.startswith("#define VK_HOST_CONNECTION(ret)")]
    if len(matches) != 1:
        raise RuntimeError("expected exactly one VK_HOST_CONNECTION macro")
    i = matches[0]
    result = []
    while True:
        result.append(lines[i])
        if not lines[i].rstrip().endswith("\\"):
            break
        i += 1
    return "\n".join(result) + "\n"


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True,
                        help="gfxstream source root containing guest/vulkan")
    parser.add_argument("--patch", type=pathlib.Path,
                        help="apply patch to a temporary source copy before testing")
    parser.add_argument("--cxx", default="c++", help="host C++ compiler")
    parser.add_argument("--compiler-arg", action="append", default=[], help="additional compiler argument")
    args = parser.parse_args()
    here = pathlib.Path(__file__).resolve().parent
    original = args.source / "guest/vulkan/gfxstream_vk_device.cpp"
    if not original.is_file():
        raise RuntimeError(f"missing source: {original}")
    with tempfile.TemporaryDirectory(prefix="rme-vk-null-conn-") as d:
        root = pathlib.Path(d)
        candidate = root / "guest/vulkan/gfxstream_vk_device.cpp"
        candidate.parent.mkdir(parents=True)
        candidate.write_bytes(original.read_bytes())
        if args.patch:
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(args.patch.resolve())],
                           cwd=root, check=True)
        macro = extract_macro(candidate)
        (root / "macro.h").write_text(macro, encoding="utf-8")
        exe = root / "macro-test"
        subprocess.run([args.cxx, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0", "-I", str(root),
                        *args.compiler_arg, str(here / "harness.cpp"), "-o", str(exe)], check=True)
        failures = []
        for mode in ("host-missing", "encoder-missing", "ok"):
            for result_type in ("int", "pointer", "void"):
                proc = subprocess.run([str(exe), mode, result_type], preexec_fn=no_core,
                                      capture_output=True, text=True, timeout=5)
                if proc.returncode != 0:
                    failures.append(f"{mode}/{result_type}: exit={proc.returncode} {proc.stderr.strip()}")
        if failures:
            print("FAILED actual macro cases:\n" + "\n".join(failures))
            return 1
        print("PASS actual macro: 3 connection states x 3 return types")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
