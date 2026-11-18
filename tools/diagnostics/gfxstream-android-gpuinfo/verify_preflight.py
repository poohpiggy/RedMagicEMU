#!/usr/bin/env python3
"""Exercise the Android GPU preflight and build its paired renderer objects."""
import argparse
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
IDENTITY = HERE.parent / "gfxstream-device-uuid"
BRIDGE = HERE.parents[2] / "native/vulkanbridge"
COMPONENTS = ("host_fixed_compute", "gpu_select", "gpu_device", "gpu_buffer",
              "gpu_submission", "gpu_compute")


def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, timeout=180, **kwargs)


def build_preflight(tools, build, out):
    out.mkdir()
    database = json.loads((build / "compile_commands.json").read_text())
    entry = next(e for e in database if e["file"].endswith("/VkCommonOperations.cpp"))
    command = shlex.split(entry["command"])
    bin_dir = Path(command[0]).parent
    ndk = bin_dir.parents[4]
    shader_tools = ndk / "shader-tools/linux-x86_64"
    source = (Path(entry["directory"]) / entry["file"]).resolve()
    patched = out / "VkCommonOperations.cpp"
    run([sys.executable, IDENTITY / "apply_identity.py", source, patched])
    selection = out / "VkCommonOperations.cpp.o"
    command[1:1] = ["-I", str(IDENTITY), "-I", str(source.parent)]
    for flag, value in (("-o", selection), ("-MF", out / "selection.d"),
                        ("-MQ", selection), ("-c", patched)):
        if flag in command:
            command[command.index(flag) + 1] = str(value)
    run(command, cwd=entry["directory"])

    shader = out / "shader.spv"
    run([shader_tools / "glslc", "-fshader-stage=compute", "--target-env=vulkan1.2",
         BRIDGE / "host_compute.comp", "-o", shader])
    run([shader_tools / "spirv-val", "--target-env", "vulkan1.2", shader])
    words = [f"0x{word:08x}u" for (word,) in struct.iter_unpack("<I", shader.read_bytes())]
    rows = (", ".join(words[i:i + 8]) for i in range(0, len(words), 8))
    (out / "host_shader_spv.h").write_text(
        "#include <stdint.h>\nstatic const uint32_t host_shader_spv[] = {\n    " +
        ",\n    ".join(rows) + "\n};\n")

    common = ["-O2", "-Wall", "-Wextra", "-Werror", "-I", BRIDGE, "-I", IDENTITY, "-I", out]
    gpu_headers = ["-isystem", tools / "src/host/gl/gl-host-common/include",
                   "-isystem", tools / "prefix/include"]
    vk_headers = ["-isystem", tools / "src/common/vulkan/include"]
    host_cxx = [tools / "bin/cxx", "-std=c++17", *common, *gpu_headers, *vk_headers]
    test_sources = [HERE / "preflight.cpp", HERE / "preflight_test.cpp"]
    fake = out / "preflight-fake"
    run([*host_cxx, "-DRME_TEST_FAKE_VULKAN", *test_sources, "-o", fake])
    for scenario in ("ok", "blacklisted", "empty-uuid", "compute-fail", "unresolved"):
        run([fake, scenario])

    host_objects = []
    android_objects = [out / "preflight.o"]
    for component in COMPONENTS:
        c_source = BRIDGE / (component + ".c")
        host_object = out / (component + "-host.o")
        android_object = out / (component + ".o")
        run(["cc", "-std=c11", *common, *vk_headers, "-c", c_source, "-o", host_object])
        run([bin_dir / "aarch64-linux-android29-clang", "-std=c11", *common, "-fPIC",
             "-c", c_source, "-o", android_object])
        host_objects.append(host_object)
        android_objects.append(android_object)
    cpu = out / "preflight-cpu"
    run([*host_cxx, *test_sources, *host_objects, "-Wl,-l:libvulkan.so.1", "-o", cpu])
    env = {key: val for key, val in os.environ.items() if not key.startswith("VK_")}
    env["VK_ICD_FILENAMES"] = "/usr/share/vulkan/icd.d/lvp_icd.json"
    result = run([cpu, "llvmpipe"], env=env, text=True, capture_output=True)
    if "selection 6)" not in result.stderr:
        raise RuntimeError(f"expected real llvmpipe to be rejected by device type: {result.stderr}")
    print(result.stdout, end="", flush=True)
    run([bin_dir / "aarch64-linux-android29-clang++", "-std=c++17", *common, *gpu_headers, "-fPIC",
         "-c", HERE / "preflight.cpp", "-o", android_objects[0]])
    return selection, android_objects


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tools", type=Path)
    parser.add_argument("build", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    build_preflight(args.tools.resolve(), args.build.resolve(), args.out.resolve())
