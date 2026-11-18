"""Compare the socket discovery result with direct, read-only OpenCL queries.

Linux/WSL only; caller must set LD_LIBRARY_PATH to an existing clvk build and
dependencies. The Vulkan ICD is forced to Mesa's llvmpipe software driver.
No OpenCL contexts, queues, programs, or kernels are created.
"""
import ctypes as c
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ICD = Path("/usr/share/vulkan/icd.d/lvp_icd.json")
CL_DEVICE_NOT_FOUND = -1
CL_DEVICE_TYPE_ALL = 0xFFFFFFFF
CL_DEVICE_TYPE_GPU = 4


def check(status, name):
    if status:
        raise RuntimeError(f"{name} failed: {status}")


def string_value(function, handle, field):
    size = c.c_size_t()
    check(function(handle, field, 0, None, c.byref(size)), "string size")
    if not 0 < size.value <= 4096:
        raise RuntimeError(f"invalid OpenCL string length {size.value}")
    value = c.create_string_buffer(size.value)
    actual = c.c_size_t()
    check(function(handle, field, size.value, value, c.byref(actual)), "string value")
    if actual.value != size.value or value.raw[-1] != 0:
        raise RuntimeError("inconsistent OpenCL string length")
    return value.value.decode("ascii")


def main():
    if not os.environ.get("LD_LIBRARY_PATH"):
        raise SystemExit("set LD_LIBRARY_PATH to an existing clvk library and its dependencies")
    icd = json.loads(ICD.read_text(encoding="utf-8"))
    if icd["ICD"]["library_path"] != "libvulkan_lvp.so":
        raise SystemExit("expected the Mesa llvmpipe software ICD")
    os.environ["VK_ICD_FILENAMES"] = str(ICD)
    os.environ["MESA_SHADER_CACHE_DISABLE"] = "true"
    os.environ["CLVK_CACHE_DIR"] = ""
    for name in ("VK_DRIVER_FILES", "VK_ADD_DRIVER_FILES", "VK_LAYER_PATH", "VK_INSTANCE_LAYERS"):
        os.environ.pop(name, None)
    if os.environ.get("LD_PRELOAD") or os.environ.get("LD_AUDIT"):
        raise SystemExit("unset LD_PRELOAD and LD_AUDIT before testing")
    with tempfile.TemporaryDirectory(prefix="rme-real-cl-discovery-") as tmp:
        os.environ["XDG_CACHE_HOME"] = tmp
        os.environ["CLVK_COMPILER_TEMP_DIR"] = tmp
        shared = Path(tmp) / "libdiscovery.so"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fPIC", "-shared", str(HERE.parent / "discovery.c"),
                        "-pthread", "-ldl", "-o", str(shared)], check=True)
        cl = c.CDLL("libOpenCL.so")
        platform_ids = cl.clGetPlatformIDs
        platform_ids.argtypes = [c.c_uint, c.POINTER(c.c_void_p), c.POINTER(c.c_uint)]
        platform_info = cl.clGetPlatformInfo
        platform_info.argtypes = [c.c_void_p, c.c_uint, c.c_size_t, c.c_void_p, c.POINTER(c.c_size_t)]
        device_ids = cl.clGetDeviceIDs
        device_ids.argtypes = [c.c_void_p, c.c_uint64, c.c_uint, c.POINTER(c.c_void_p), c.POINTER(c.c_uint)]
        device_info = cl.clGetDeviceInfo
        device_info.argtypes = [c.c_void_p, c.c_uint, c.c_size_t, c.c_void_p, c.POINTER(c.c_size_t)]
        count = c.c_uint()
        check(platform_ids(0, None, c.byref(count)), "clGetPlatformIDs count")
        if not 0 < count.value <= 32:
            raise RuntimeError(f"invalid platform count {count.value}")
        platforms = (c.c_void_p * count.value)()
        check(platform_ids(count, platforms, None), "clGetPlatformIDs")
        devices = []
        for platform in platforms:
            n = c.c_uint()
            error = device_ids(platform, CL_DEVICE_TYPE_ALL, 0, None, c.byref(n))
            if error == CL_DEVICE_NOT_FOUND:
                continue
            check(error, "clGetDeviceIDs count")
            if not 0 < n.value <= 128:
                raise RuntimeError(f"invalid device count {n.value}")
            ids = (c.c_void_p * n.value)()
            check(device_ids(platform, CL_DEVICE_TYPE_ALL, n, ids, None), "clGetDeviceIDs")
            for device in ids:
                kind = c.c_uint64()
                actual = c.c_size_t()
                check(device_info(device, 0x1000, c.sizeof(kind), c.byref(kind), c.byref(actual)),
                      "CL_DEVICE_TYPE")
                if actual.value != c.sizeof(kind):
                    raise RuntimeError("invalid CL_DEVICE_TYPE length")
                devices.append((platform, device, kind.value))
        if not devices:
            raise RuntimeError("no OpenCL device")
        platform, device, kind = next((d for d in devices if d[2] & CL_DEVICE_TYPE_GPU), devices[0])
        p_name = string_value(platform_info, platform, 0x0902)
        d_name = string_value(device_info, device, 0x102B)
        d_version = string_value(device_info, device, 0x102F)
        if kind & CL_DEVICE_TYPE_GPU:
            raise RuntimeError("llvmpipe unexpectedly reported a GPU; refusing a hardware claim")
        native = c.CDLL(str(shared))
        native.opencl_discovery_roundtrip.argtypes = [c.c_char_p, c.c_size_t]
        native.opencl_discovery_roundtrip.restype = c.c_int
        result = c.create_string_buffer(1024)
        check(native.opencl_discovery_roundtrip(result, len(result)), "socket discovery")
        observed = result.value.decode("ascii")
        expected = f"platform={p_name} device={d_name} version={d_version} type=0x{kind:x}"
        if expected not in observed:
            raise RuntimeError(f"direct/socket mismatch: expected {expected!r}, got {observed!r}")
        print("Direct OpenCL and socket discovery agree:")
        print(expected)
        print("Mesa llvmpipe software CPU; no GPU hardware or kernel execution tested")


if __name__ == "__main__":
    main()
