#!/usr/bin/env python3
"""Exercise actual generated surface output writes and destroy driver call."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("guest", type=Path)
parser.add_argument("host", type=Path)
args = parser.parse_args()
guest, host = args.guest.read_text(), args.host.read_text()

def encoder(name):
    start = guest.index("VkEncoder::" + name + "(")
    end = guest.index("  if (watchdog) {", start)
    return guest[start:end]

def statement(text, pattern):
    found = re.findall(pattern, text, re.S)
    if len(found) != 1:
        raise SystemExit("unexpected generated statement: " + pattern)
    return found[0]

caps = encoder("vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
formats = encoder("vkGetPhysicalDeviceSurfaceFormatsKHR")
modes = encoder("vkGetPhysicalDeviceSurfacePresentModesKHR")
caps_write = statement(caps, r"(reservedmarshal_VkSurfaceCapabilitiesKHR\([^;]+;)")
format_write = statement(formats, r"(reservedmarshal_VkSurfaceFormatKHR\([^;]+;)")
mode_write = statement(modes, r"((?:memcpy|memset)\(\*streamPtrPtr,[^;]*sizeof\(VkPresentModeKHR\)\);)")
format_count = statement(formats, r"(memcpy\(\*streamPtrPtr,\s*\(uint32_t \*\)[^;]+sizeof\(uint32_t\)\);)")
mode_count = statement(modes, r"(memcpy\(\*streamPtrPtr,\s*\(uint32_t \*\)[^;]+sizeof\(uint32_t\)\);)")
start = host.index("case OP_vkDestroySurfaceKHR:")
end = host.index("case OP_", start + 8)
destroy_case = host[start:end]
adapter_mode = "rme_vk_guest_surface_destroy" in destroy_case
if adapter_mode:
    destroy = statement(destroy_case, r"(const bool rmeDestroyed = rme_vk_guest_surface_destroy\([^;]+;)")
    delete = statement(destroy_case, r"(if \(rmeDestroyed\) delete_VkSurfaceKHR\([^;]+;)")
else:
    destroy = statement(destroy_case, r"(vk->vkDestroySurfaceKHR\([^;]+;)")
    delete = ""

program = r'''
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t VkPresentModeKHR;
struct VkSurfaceCapabilitiesKHR { uint32_t value; };
struct VkSurfaceFormatKHR { uint32_t value; };
struct VkAllocationCallbacks { int unused; };
static const int VK_STRUCTURE_TYPE_MAX_ENUM = 0;
static void reservedmarshal_VkSurfaceCapabilitiesKHR(void*,int,VkSurfaceCapabilitiesKHR* p,uint8_t** o) { memcpy(*o,p,sizeof(*p)); }
static void reservedmarshal_VkSurfaceFormatKHR(void*,int,VkSurfaceFormatKHR* p,uint8_t** o) { memcpy(*o,p,sizeof(*p)); }
struct Driver {
    const VkAllocationCallbacks* received = nullptr;
    void vkDestroySurfaceKHR(int, int, const VkAllocationCallbacks* p) { received = p; }
};
[[maybe_unused]] static bool adapter_ok;
[[maybe_unused]] static int adapter_calls, delete_calls;
[[maybe_unused]] static bool rme_vk_guest_surface_destroy(int instance, int surface) {
    if (instance != 1 || surface != 2) return false;
    ++adapter_calls;
    return adapter_ok;
}
[[maybe_unused]] static void delete_VkSurfaceKHR(int surface) {
    if (surface == 3) ++delete_calls;
}
int main(int argc, char** argv) {
    if (argc != 2) return 20;
    int which = argv[1][0] - '0';
    uint8_t data[8] = {};
    uint8_t* ptr = data;
    uint8_t** streamPtrPtr = &ptr;
    void* stream = nullptr;
    VkSurfaceCapabilitiesKHR caps{0xabababab}, rmeEmptyCapabilities{};
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities = &caps;
    (void)pSurfaceCapabilities; (void)rmeEmptyCapabilities;
''' + caps_write + r'''
    uint32_t actual; memcpy(&actual, data, sizeof(actual));
    if (actual && which == 1) return 1;
    memset(data, 0, sizeof(data));
    VkSurfaceFormatKHR fs[] = {{0xcdcdcdcd}}, rmeEmptySurfaceFormat{};
    VkSurfaceFormatKHR* pSurfaceFormats = fs;
    uint32_t i = 0;
    (void)pSurfaceFormats; (void)i; (void)rmeEmptySurfaceFormat;
''' + format_write + r'''
    memcpy(&actual, data, sizeof(actual)); if (actual && which == 2) return 2;
    memset(data, 0, sizeof(data));
    VkPresentModeKHR mode = 0xefefefef;
    VkPresentModeKHR* pPresentModes = &mode;
    uint32_t n = 1; uint32_t* pPresentModeCount = &n;
    (void)pPresentModes;
''' + mode_write + r'''
    memcpy(&actual, data, sizeof(actual)); if (actual && which == 3) return 3;
    memset(data, 0, sizeof(data));
    uint32_t* pSurfaceFormatCount = &n;
    pSurfaceFormats = nullptr;
    uint32_t rmeFormatRequestCount = pSurfaceFormats ? *pSurfaceFormatCount : 0;
    (void)rmeFormatRequestCount;
''' + format_count + r'''
    memcpy(&actual, data, sizeof(actual)); if (actual && which == 4) return 4;
    memset(data, 0, sizeof(data));
    pPresentModes = nullptr;
    uint32_t rmeModeRequestCount = pPresentModes ? *pPresentModeCount : 0;
    (void)rmeModeRequestCount;
''' + mode_count + r'''
    memcpy(&actual, data, sizeof(actual)); if (actual && which == 5) return 5;
    Driver driver; Driver* vk = &driver;
    VkAllocationCallbacks guest{};
    const VkAllocationCallbacks* pAllocator = &guest;
    int unboxed_instance = 1, surface = 2, boxed_surface_preserve = 3;
    (void)pAllocator; (void)vk; (void)boxed_surface_preserve;
    adapter_ok = which != 9;
''' + destroy + r'''
''' + delete + r'''
    if (adapter_calls && which == 6 && delete_calls != 1) return 6;
    if (which == 9 && (adapter_calls != 1 || delete_calls != 0)) return 9;
    if (driver.received && which == 6) return 6;
    memset(data, 0, sizeof(data));
    pSurfaceFormats = fs;
    rmeFormatRequestCount = pSurfaceFormats ? *pSurfaceFormatCount : 0;
''' + format_count + r'''
    memcpy(&actual, data, sizeof(actual)); if (actual != 1 && which == 7) return 7;
    memset(data, 0, sizeof(data));
    pPresentModes = &mode;
    rmeModeRequestCount = pPresentModes ? *pPresentModeCount : 0;
''' + mode_count + r'''
    memcpy(&actual, data, sizeof(actual)); if (actual != 1 && which == 8) return 8;
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="rme-surface-outputs-") as tmp:
    root = Path(tmp)
    source = root / "test.cpp"
    source.write_text(program)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-nostdlib++", str(source), "-o", str(root / "test")], check=True)
    failures = [n for n in range(1, 10 if adapter_mode else 9)
                if subprocess.run([str(root / "test"), str(n)], cwd=root).returncode]
    if failures:
        raise SystemExit("FAILED_CASES " + ",".join(map(str, failures)))
print("SURFACE_OUTPUTS_AND_HOST_ALLOCATOR_OK")
