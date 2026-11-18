#!/usr/bin/env python3
"""Compare the actual d7a initialization control flow before and after the patch.

The supplied host build and dependencies are pre-existing; this script downloads
nothing. Queue-family and device metadata are synthetic; execution stops before
device creation or resource allocation. All edits stay in a temporary copy.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

EXPECTED_SOURCE = "0e9860f5df620e63d3bb1055a56e800c7792b89cca8af7b19579a41e5e8e53c1"
TARGET = "host/libgfxstream_backend.so.0.1.2"
SOURCE_FILE = Path("host/vulkan/VkCommonOperations.cpp")
ICD = Path("/usr/share/vulkan/icd.d/lvp_icd.json")


from instrument import instrument


def run(cmd, env, expected=0, cwd=None):
    result = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=90, cwd=cwd)
    if result.returncode != expected:
        raise RuntimeError(f"{cmd} returned {result.returncode}, expected {expected}:\n{result.stdout}")
    return result.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, help="fixed d7a source directory")
    ap.add_argument("host_build", type=Path, help="existing Linux host build for this source")
    ap.add_argument("ninja", type=Path)
    ap.add_argument("dependency_libs", help="colon-separated existing runtime library directories")
    ap.add_argument("sysroot_include", type=Path)
    args = ap.parse_args()
    args.source = args.source.resolve(strict=True)
    args.host_build = args.host_build.resolve(strict=True)
    args.ninja = args.ninja.resolve(strict=True)
    args.sysroot_include = args.sysroot_include.resolve(strict=True)
    if hashlib.sha256((args.source / SOURCE_FILE).read_bytes()).hexdigest() != EXPECTED_SOURCE:
        raise ValueError("source file does not match the examined d7a baseline")
    if not ICD.is_file() or '"libvulkan_lvp.so"' not in ICD.read_text():
        raise ValueError("expected software-only lavapipe ICD is unavailable")

    env = os.environ.copy()
    env.pop("ANDROID_EMU_VK_ICD", None)
    env.update(VK_DRIVER_FILES=str(ICD), VK_ICD_FILENAMES=str(ICD),
               LIBGL_ALWAYS_SOFTWARE="1", GALLIUM_DRIVER="llvmpipe",
               MESA_SHADER_CACHE_DISABLE="true", CPLUS_INCLUDE_PATH=str(args.sysroot_include))
    with tempfile.TemporaryDirectory(prefix="rme-device-selection-test-", dir="/var/tmp") as temp:
        root = Path(temp)
        run(["cp", "-a", "--reflink=auto", str(args.source), str(root / "src")], env)
        run(["cp", "-a", "--reflink=auto", str(args.host_build), str(root / "build-host")], env)
        source = root / "src" / SOURCE_FILE
        instrument(source)
        build = root / "build-host"
        env["LD_LIBRARY_PATH"] = str(build / "host") + ":" + args.dependency_libs
        dry = run([str(args.ninja), "-n", "-C", str(build), TARGET], env)
        if "Regenerating build files" in dry or "VkCommonOperations.cpp" not in dry:
            raise RuntimeError("copied build does not rebuild just the instrumented source:\n" + dry)

        check = '''import ctypes,sys
l=ctypes.CDLL(sys.argv[1]); f=l.rme_audit_device_selection
f.argtypes=[ctypes.c_int]; f.restype=ctypes.c_int
value=f(int(sys.argv[2])); print("AUDIT_VALUE=%d" % value,flush=True)
sys.exit(0 if value==int(sys.argv[3]) else 1)
'''
        lib = str(build / TARGET)

        def build_and_check(expected):
            build_output = run([str(args.ninja), "-j2", "-C", str(build), TARGET], env)
            symbols = run(["nm", "-D", lib], env)
            if "rme_audit_device_selection" not in symbols:
                raise RuntimeError("instrumented symbol absent from rebuilt backend:\n" + build_output)
            for mode, value in enumerate(expected):
                output = run([sys.executable, "-c", check, lib, str(mode), str(value)], env)
                if "AUDIT_BACKING=llvmpipe" not in output or "type=4" not in output:
                    raise RuntimeError("test backing device is not the expected llvmpipe CPU:\n" + output)

        # 0 CPU, 1 virtual, 2/3 multi-device, 4 API 1.1, 5 graphics only,
        # 6 split families, 7 combined second family, 8 zero queues, 9 preference,
        # 10 nonzero Vulkan API variant.
        build_and_check([0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0])
        print("BASELINE: rejected only zero queues; selected ineligible candidates and wrong queue")
        patch = Path(__file__).resolve().with_name("fix.patch")
        source.write_bytes((args.source / SOURCE_FILE).read_bytes())
        run(["patch", "--fuzz=0", "-p1", "-i", str(patch)], env, cwd=root / "src")
        instrument(source)
        build_and_check([-1, -1, 100, 100, -1, -1, -1, 1, -1, 0, -1])
        print("PATCHED: candidate eligibility, fallback, preference and queue selection matched all 11 cases")


if __name__ == "__main__":
    main()
