#!/usr/bin/env python3
"""Test actual generated image responses and per-swapchain wrapper lifetime."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('generated', type=Path)
args = parser.parse_args()
text = (args.generated / 'guest/VkEncoder.cpp').read_text()
begin_marker = '// RME swapchain image wrapper cache begin'
end_marker = '// RME swapchain image wrapper cache end'
if begin_marker in text and end_marker in text:
    helpers = text[text.index(begin_marker):text.index(end_marker) + len(end_marker)]
else:
    helpers = 'static void rmeReleaseSwapchainImages(VkSwapchainKHR, VulkanHandleMapping*) {}'
start = text.index('VkEncoder::vkGetSwapchainImagesKHR(')
end = text.index('VkEncoder::vkAcquireNextImageKHR(', start)
body = text[start:end]
watchdog = body.index('  if (watchdog) {', body.index('  uint32_t opcode_vkGetSwapchainImagesKHR'))
start = body.find('  /* RME image response */;', watchdog)
if start < 0:
    start = body.index('  // WARNING PTR CHECK', watchdog)
end = body.index('  ++encodeCount;', start)
response = body[start:end]
start = text.index('VkEncoder::vkDestroySwapchainKHR(')
end = text.index('VkEncoder::vkGetSwapchainImagesKHR(', start)
destroy = text[start:end]
release = 'rmeReleaseSwapchainImages(swapchain, sResourceTracker->destroyMapping());'
cleanup_connected = release in destroy and destroy.index(release) < destroy.index('mapHandles_VkSwapchainKHR')

program = r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <mutex>
#include <unordered_map>
#include <vector>
using VkResult = int32_t; using VkSwapchainKHR = uint64_t; using VkImage = uint64_t;
constexpr VkResult VK_SUCCESS = 0, VK_INCOMPLETE = 5, VK_ERROR_DEVICE_LOST = -4;
constexpr VkResult VK_ERROR_INITIALIZATION_FAILED = -3;
constexpr VkImage VK_NULL_HANDLE = 0;
static bool observeAllocations = false;
static size_t largestAllocation = 0;
void* operator new(size_t n) {
    if (observeAllocations && n > largestAllocation) largestAllocation = n;
    if (void* p = malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
static int created = 0, destroyed = 0;
struct VulkanHandleMapping {
    bool create;
    void mapHandles_u64_VkImage(const uint64_t* wire, VkImage* out, size_t n) {
        for (size_t i = 0; i < n; ++i) out[i] = create ? 0x10000 + ++created : wire[i];
    }
    void mapHandles_VkImage(VkImage*, size_t n) { destroyed += n; }
} ordinary{false}, make{true}, releaseMap{false};
struct Tracker {
    VulkanHandleMapping* createMapping() { return &make; }
    VulkanHandleMapping* destroyMapping() { return &releaseMap; }
} tracker;
static Tracker* sResourceTracker = &tracker;
''' + helpers + r'''
struct Wire {
    uint8_t bytes[2048]{}; size_t pos = 0, used = 0, maxRead = 0, fragments = 0;
    std::vector<uint8_t> scratch;
    VulkanHandleMapping* mapping = &ordinary;
    void add(const void* p,size_t n) { memcpy(bytes+used,p,n); used += n; }
    uint64_t getBe64() { uint64_t v; read(&v,8); return v; }
    void read(void* p,size_t n) {
        if (pos+n > used) __builtin_trap();
        if (n > maxRead) maxRead = n;
        uint8_t* dst = (uint8_t*)p;
        while (n) { size_t part = n > 3 ? 3 : n; memcpy(dst,bytes+pos,part); pos += part; dst += part; n -= part; ++fragments; }
    }
    void alloc(void** p,size_t n) { scratch.resize(n); *p = scratch.data(); }
    void setHandleMapping(VulkanHandleMapping* m) { mapping = m; }
    void unsetHandleMapping() { mapping = &ordinary; }
    VulkanHandleMapping* handleMapping() { return mapping; }
};
static VkResult readResult(Wire* stream, VkSwapchainKHR swapchain,
                           uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) {
    uint32_t rmeImageCapacity = pSwapchainImages ? *pSwapchainImageCount : 0;
''' + response + r'''
    return vkGetSwapchainImagesKHR_VkResult_return;
}
static VkResult enumerate(VkSwapchainKHR swapchain, uint32_t count, const uint64_t* host,
                          VkImage* images, VkResult result) {
    Wire stream; uint64_t ptr = 1; stream.add(&ptr,8); stream.add(&count,4);
    stream.add(&ptr,8); stream.add(host,count*8); stream.add(&result,4);
    uint32_t actual = count;
    VkResult returned = readResult(&stream, swapchain, &actual, images);
    return actual == count ? returned : VK_ERROR_INITIALIZATION_FAILED;
}
int main() {
    uint64_t host[2] = {101,102}; VkImage a[2]{}, b[2]{}, other[2]{};
    if (enumerate(7,2,host,a,VK_SUCCESS) != VK_SUCCESS || created != 2) return 1;
    if (enumerate(7,2,host,b,VK_SUCCESS) != VK_SUCCESS || created != 2 ||
        a[0] != b[0] || a[1] != b[1]) return 2;
    if (enumerate(7,1,host,b,VK_INCOMPLETE) != VK_INCOMPLETE || b[0] != a[0] || created != 2) return 3;
    if (enumerate(8,1,host,other,VK_SUCCESS) != VK_SUCCESS || other[0] == a[0] || created != 3) return 4;
    rmeReleaseSwapchainImages(7, sResourceTracker->destroyMapping());
    if (destroyed != 2) return 5;
    rmeReleaseSwapchainImages(7, sResourceTracker->destroyMapping());
    if (destroyed != 2) return 6;
    if (enumerate(7,1,host,b,VK_SUCCESS) != VK_SUCCESS || b[0] == a[0] || created != 4) return 7;
    VkImage unchanged = 0xfeed;
    if (enumerate(7,1,host,&unchanged,VK_ERROR_DEVICE_LOST) != VK_ERROR_DEVICE_LOST ||
        unchanged != 0xfeed || created != 4) return 8;
    rmeReleaseSwapchainImages(7, sResourceTracker->destroyMapping());
    rmeReleaseSwapchainImages(8, sResourceTracker->destroyMapping());
    if (destroyed != 4) return 9;
    uint64_t nullHost = 0; unchanged = 0xfeed;
    if (enumerate(9,1,&nullHost,&unchanged,VK_SUCCESS) != VK_ERROR_INITIALIZATION_FAILED ||
        unchanged != 0xfeed || created != 4) return 10;
    Wire oversize; uint64_t ptr = 1, hosts[70];
    for (uint64_t& id : hosts) id = 101;
    uint32_t returnedCount = 70, capacity = 1;
    VkResult success = VK_SUCCESS; uint64_t sentinel = 0xabcdef9876543210ULL;
    oversize.add(&ptr,8); oversize.add(&returnedCount,4); oversize.add(&ptr,8);
    oversize.add(hosts,sizeof(hosts)); oversize.add(&success,4); oversize.add(&sentinel,8);
    unchanged = 0xfeed; largestAllocation = 0; observeAllocations = true;
    VkResult malformed = readResult(&oversize, 10, &capacity, &unchanged);
    observeAllocations = false;
    if (malformed != VK_ERROR_INITIALIZATION_FAILED || unchanged != 0xfeed || created != 4 ||
        largestAllocation || oversize.maxRead > 256 || oversize.fragments < 70) return 11;
    if (oversize.getBe64() != sentinel) return 12;
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='rme-images-response-') as temp:
    root = Path(temp)
    cpp = root / 'test.cpp'
    cpp.write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-variable', str(cpp), '-pthread', '-o', str(root / 'test')], check=True)
    result = subprocess.run([str(root / 'test')])
    print('IMAGES_RESPONSE_CASE', result.returncode, flush=True)
    if result.returncode:
        raise SystemExit('image wrapper response or lifetime failed')
    if not cleanup_connected:
        raise SystemExit('swapchain destroy does not release image wrappers before destroying swapchain')
print('SWAPCHAIN_IMAGES_STABLE_WRAPPERS_AND_CLEANUP_OK')
