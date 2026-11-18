#!/usr/bin/env python3
"""Exercise the real d7a instance extension control flow without Vulkan/GPU."""
import argparse
from pathlib import Path
import resource
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--patch", type=Path)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--compiler-arg", action="append", default=[])
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    relative = Path("host/vulkan/VkDecoderGlobalState.cpp")
    with tempfile.TemporaryDirectory(prefix="rme-instance-surface-") as temp:
        root = Path(temp)
        target = root / relative
        target.parent.mkdir(parents=True)
        target.write_bytes((args.source / relative).read_bytes())
        if args.patch:
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(args.patch.resolve())], cwd=root, check=True)
        text = target.read_text()
        start = text.index("    VkResult on_vkCreateInstance(")
        end = text.index("        // Create higher version instance whenever it is possible.", start)
        create = text[start:end] + '''
        (void)pool; (void)pAllocator; (void)pInstance;
        for (const char* ext : finalExts) created.emplace_back(ext);
        return VK_SUCCESS;
    }
'''
        start = text.index("    std::vector<const char*> filteredInstanceExtensionNames(")
        end = text.index("    bool getDefaultQueueForDeviceLocked(", start)
        methods = text[start:end]
        (root / "methods.inc").write_text(create + methods)
        exe = root / "test"
        subprocess.run([args.cxx, "-std=c++17", "-D__ANDROID__", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-I", str(root), *args.compiler_arg,
                        str(here / "harness.cpp"), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], text=True, capture_output=True,
                                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        if result.returncode:
            print(f"FAIL extracted host instance control flow: {result.stderr.strip()}")
            return 1
        print("PASS: requested host extensions, deduplication, missing features, failure propagation and compute-only path")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
