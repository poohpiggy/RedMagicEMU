"""Build the same Vulkan compute probe for WSL and Android without a device."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, timeout=90, **kwargs)


def linux_path(path):
    return run(["wsl.exe", "--exec", "wslpath", "-a", str(path)], capture_output=True, text=True).stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", required=True, type=Path, help="Windows Android NDK root")
    parser.add_argument("--android-output", type=Path,
                        help="optional path for the ARM64 executable; must not already exist")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("this runner requires Windows, WSL and a Windows Android NDK")
    ndk = args.ndk.resolve()
    prebuilt = ndk / "toolchains/llvm/prebuilt/windows-x86_64"
    clang = prebuilt / "bin/clang.exe"
    headers = prebuilt / "sysroot/usr/include"
    glslc = ndk / "shader-tools/windows-x86_64/glslc.exe"
    spirv_val = ndk / "shader-tools/windows-x86_64/spirv-val.exe"
    for path in (clang, glslc, spirv_val, headers / "vulkan/vulkan.h"):
        if not path.is_file():
            parser.error(f"required NDK component not found: {path}")

    with tempfile.TemporaryDirectory(prefix="rme-vulkan-compute-") as directory:
        temp = Path(directory)
        spv = temp / "compute.spv"
        run([glslc, "-fshader-stage=compute", "--target-env=vulkan1.2", ROOT / "compute.comp", "-o", spv])
        run([spirv_val, "--target-env", "vulkan1.2", spv])
        data = spv.read_bytes()
        if len(data) < 20 or len(data) % 4:
            raise RuntimeError("invalid SPIR-V size")
        words = struct.unpack(f"<{len(data) // 4}I", data)
        if words[0] != 0x07230203:
            raise RuntimeError("invalid SPIR-V magic")
        (temp / "shader_spv.h").write_text(
            "#include <stdint.h>\nstatic const uint32_t shader_spv[] = {\n" +
            ",\n".join("    " + ", ".join(f"0x{x:08x}u" for x in words[i:i + 8])
                        for i in range(0, len(words), 8)) + "\n};\n", encoding="ascii")

        android = temp / "vulkan-compute-android-arm64"
        run([clang, "--target=aarch64-linux-android29", "-Wl,-z,max-page-size=16384",
             "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-I", temp,
             ROOT / "probe.c", "-lvulkan", "-o", android])
        print("ANDROID_ARM64_BUILD_OK sha256=" + hashlib.sha256(android.read_bytes()).hexdigest(), flush=True)

        include = temp / "include"
        include.mkdir()
        shutil.copytree(headers / "vulkan", include / "vulkan")
        if (headers / "vk_video").is_dir():
            shutil.copytree(headers / "vk_video", include / "vk_video")
        linux_temp, linux_root, linux_include = map(linux_path, (temp, ROOT, include))
        desktop = linux_temp + "/probe"
        wait_fault = linux_temp + "/probe_wait_fault"
        validator = linux_temp + "/verify_test"
        rank_test = linux_temp + "/device_rank_test"
        run(["wsl.exe", "--exec", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", linux_root,
             linux_root + "/verify_test.c", "-o", validator])
        run(["wsl.exe", "--exec", validator])
        run(["wsl.exe", "--exec", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", linux_include, linux_root + "/device_rank_test.c", "-o", rank_test])
        run(["wsl.exe", "--exec", rank_test])
        run(["wsl.exe", "--exec", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
             "-I", linux_temp, "-I", linux_include, linux_root + "/probe.c", "-Wl,-l:libvulkan.so.1", "-o", desktop])
        run(["wsl.exe", "--exec", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
             "-DRME_TEST_WAIT_FAILURE", "-I", linux_temp, "-I", linux_include, linux_root + "/probe.c",
             "-Wl,-l:libvulkan.so.1", "-o", wait_fault])
        command = ["wsl.exe", "--exec", "env", "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json", desktop]
        fault = subprocess.run(command[:-1] + [wait_fault], capture_output=True, text=True, timeout=60)
        if (fault.returncode != 1 or "FAIL vkWaitForFences" not in fault.stderr or
                "QUEUE_COMPLETION_UNKNOWN" not in fault.stderr or "VULKAN_COMPUTE_OK" in fault.stdout):
            raise RuntimeError("injected queue-wait failure was not handled safely: " + fault.stdout + fault.stderr)
        print("INJECTED_WAIT_FAILURE_REJECTED", flush=True)
        failure = subprocess.run(command + ["--expect-mismatch"], capture_output=True, text=True, timeout=60)
        if failure.returncode != 1 or "intentional expectation mismatch" not in failure.stderr:
            raise RuntimeError("negative output check did not fail as expected: " + failure.stdout + failure.stderr)
        print("INTENTIONAL_OUTPUT_MISMATCH_REJECTED", flush=True)
        success = run(command, capture_output=True, text=True)
        if "type=CPU software" not in success.stdout or "VULKAN_COMPUTE_OK words=64" not in success.stdout:
            raise RuntimeError("unexpected WSL device or incomplete verification: " + success.stdout + success.stderr)
        print(success.stdout, end="", flush=True)
        if args.android_output:
            with args.android_output.open("xb") as output:
                output.write(android.read_bytes())
            print(f"ANDROID_ARM64_EXPORTED {args.android_output.resolve()}", flush=True)
    print("Temporary build files removed; no device was accessed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
