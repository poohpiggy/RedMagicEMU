"""Computer-only regression using the real Vulkan probe and WSL lavapipe."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ADAPTER = HERE.parent / "adapter.c"


def run(cmd, **kwargs):
    return subprocess.run([str(x) for x in cmd], timeout=90, **kwargs)


def wsl(path):
    return run(["wsl.exe", "--exec", "wslpath", "-a", path], check=True,
               capture_output=True, text=True).stdout.strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", required=True, type=Path)
    args = parser.parse_args()
    if not ADAPTER.is_file():
        raise AssertionError("host Vulkan probe adapter is not implemented")
    ndk = args.ndk.resolve()
    glslc = ndk / "shader-tools/windows-x86_64/glslc.exe"
    spirv_val = ndk / "shader-tools/windows-x86_64/spirv-val.exe"
    headers = ndk / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include"
    with tempfile.TemporaryDirectory(prefix="rme-vulkan-host-test-") as name:
        temp = Path(name)
        spv = temp / "shader.spv"
        source = ROOT / "tools/diagnostics/vulkan-compute/compute.comp"
        run([glslc, "-fshader-stage=compute", "--target-env=vulkan1.2", source, "-o", spv], check=True)
        run([spirv_val, "--target-env", "vulkan1.2", spv], check=True)
        data = spv.read_bytes()
        assert len(data) >= 20 and len(data) % 4 == 0
        words = struct.unpack(f"<{len(data) // 4}I", data)
        assert words[0] == 0x07230203
        (temp / "shader_spv.h").write_text(
            "#include <stdint.h>\nstatic const uint32_t shader_spv[] = {\n" +
            ",".join(f"0x{x:08x}u" for x in words) + "\n};\n", encoding="ascii")
        include = temp / "include"
        include.mkdir()
        shutil.copytree(headers / "vulkan", include / "vulkan")
        if (headers / "vk_video").is_dir():
            shutil.copytree(headers / "vk_video", include / "vk_video")
        tmp, inc, adapter, harness = map(wsl, (temp, include, ADAPTER, HERE / "harness.c"))
        for mode in ("normal", "fault"):
            binary = tmp + "/" + mode
            flags = ["-DRME_TEST_WAIT_FAILURE"] if mode == "fault" else []
            run(["wsl.exe", "--exec", "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", tmp, "-I", inc, *flags, adapter, harness,
                 "-Wl,-l:libvulkan.so.1", "-o", binary], check=True)
        base = ["wsl.exe", "--exec", "env", "VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json"]
        ok = run(base + [tmp + "/normal"], capture_output=True, text=True)
        assert ok.returncode == 0 and "type=CPU software" in ok.stdout and "VULKAN_COMPUTE_OK words=64" in ok.stdout, ok.stdout + ok.stderr
        mismatch = run(base + [tmp + "/normal", "mismatch"], capture_output=True, text=True)
        assert mismatch.returncode == 1 and "FAIL output mismatches=" in mismatch.stdout and "VULKAN_COMPUTE_OK" not in mismatch.stdout, mismatch.stdout + mismatch.stderr
        fault = run(base + [tmp + "/fault", "repeat"], capture_output=True, text=True)
        assert fault.returncode == 1 and "FAIL vkWaitForFences" in fault.stdout and "QUEUE_COMPLETION_UNKNOWN" in fault.stdout and "SECOND_STATUS=-1" in fault.stdout and "VULKAN_COMPUTE_OK" not in fault.stdout, fault.stdout + fault.stderr
        missing = run(["wsl.exe", "--exec", "env", "VK_ICD_FILENAMES=/nonexistent/rme-icd.json", tmp + "/normal"], capture_output=True, text=True)
        assert missing.returncode == 1 and "FAIL" in missing.stdout and "VULKAN_COMPUTE_OK" not in missing.stdout, missing.stdout + missing.stderr
    print("HOST_VULKAN_PROBE_TEST_OK: CPU compute, mismatch, timeout, repeated timeout, missing ICD")


if __name__ == "__main__":
    main()
