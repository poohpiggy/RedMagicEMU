#!/usr/bin/env python3
"""Compare the actual d7a initialization control flow before and after the patch.

The supplied host build and dependencies are pre-existing; this script downloads
nothing. Device creation is replaced by a test function and execution stops
before device dispatch or resource creation. All edits stay in a temporary copy.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

EXPECTED_SOURCE = "0e9860f5df620e63d3bb1055a56e800c7792b89cca8af7b19579a41e5e8e53c1"
TARGET = "host/libgfxstream_backend.so.0.1.2"
SOURCE_FILE = Path("host/vulkan/VkCommonOperations.cpp")
ICD = Path("/usr/share/vulkan/icd.d/lvp_icd.json")


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("unexpected source layout: " + old[:80])
    return text.replace(old, new, 1)


def run(cmd, env, expected=0, cwd=None):
    result = subprocess.run(cmd, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=90, cwd=cwd)
    if result.returncode != expected:
        raise RuntimeError(f"{cmd} returned {result.returncode}, expected {expected}:\n{result.stdout}")
    return result.stdout


def instrument(source):
    text = source.read_text()
    old = "VkEmulation* createGlobalVkEmulation(VulkanDispatch* vk, gfxstream::host::FeatureSet features) {"
    new = '''static int auditDeviceMode = 0;
static int auditDeviceStop = 0;
static int auditDeviceCalls = 0;
static VKAPI_ATTR VkResult VKAPI_CALL auditCreateDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice* out) {
    ++auditDeviceCalls;
    if (auditDeviceMode == 1) return VK_ERROR_INITIALIZATION_FAILED;
    *out = reinterpret_cast<VkDevice>(static_cast<uintptr_t>(0x1234));
    return VK_SUCCESS;
}
''' + old
    text = replace_once(text, old, new)
    plain_call = "    ivk->vkCreateDevice(sVkEmulation->physdev, &dCi, nullptr, &sVkEmulation->device);"
    assigned_call = "    res = " + plain_call.strip()
    old = assigned_call if assigned_call in text else plain_call
    text = replace_once(text, old, "    if (auditDeviceMode) ivk->vkCreateDevice = auditCreateDevice;\n" + old)
    old = "    // device created; populate dispatch table\n    sVkEmulation->dvk = new VulkanDispatch;"
    new = '''    if (auditDeviceMode) {
        auditDeviceStop = sVkEmulation->device ? 2 : 1;
        return nullptr; // Stop before device dispatch and any resource allocation.
    }
''' + old
    text = replace_once(text, old, new)
    old = "VkEmulation* getGlobalVkEmulation() {"
    new = '''extern "C" __attribute__((visibility("default"))) int rme_audit_create_device(int mode) {
    auditDeviceMode = mode;
    auditDeviceStop = auditDeviceCalls = 0;
    createGlobalVkEmulation(vkDispatch(false), gfxstream::host::FeatureSet{});
    return 10 * auditDeviceCalls + auditDeviceStop;
}
''' + old
    text = replace_once(text, old, new)
    source.write_text(text)


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
    with tempfile.TemporaryDirectory(prefix="rme-createdevice-test-", dir="/var/tmp") as temp:
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
l=ctypes.CDLL(sys.argv[1]); f=l.rme_audit_create_device
f.argtypes=[ctypes.c_int]; f.restype=ctypes.c_int
value=f(int(sys.argv[2])); print("AUDIT_VALUE=%d" % value,flush=True)
sys.exit(0 if value==int(sys.argv[3]) else 1)
'''
        lib = str(build / TARGET)

        def build_and_check(negative_expected):
            build_output = run([str(args.ninja), "-j2", "-C", str(build), TARGET], env)
            symbols = run(["nm", "-D", lib], env)
            if "rme_audit_create_device" not in symbols:
                raise RuntimeError("instrumented symbol absent from rebuilt backend:\n" + build_output)
            negative = run([sys.executable, "-c", check, lib, "1", str(negative_expected)], env)
            positive = run([sys.executable, "-c", check, lib, "2", "12"], env)
            if "llvmpipe" not in negative or "llvmpipe" not in positive:
                raise RuntimeError("the selected test device was not llvmpipe")
            return negative

        before = build_and_check(11)  # called once and continued with a null device
        if "Failed to create Vulkan device" in before:
            raise RuntimeError("unexpected baseline error branch")
        print("BASELINE: failure incorrectly reached device-dispatch boundary; success reached boundary")
        patch = Path(__file__).resolve().with_name("fix.patch")
        source.write_bytes((args.source / SOURCE_FILE).read_bytes())
        run(["patch", "--fuzz=0", "-p1", "-i", str(patch)], env, cwd=root / "src")
        instrument(source)
        after = build_and_check(10)  # called once and returned before the boundary
        if "Failed to create Vulkan device. Error VK_ERROR_INITIALIZATION_FAILED" not in after:
            raise RuntimeError("failure did not propagate the expected error")
        print("PATCHED: failure returned before device dispatch; success reached boundary")


if __name__ == "__main__":
    main()
