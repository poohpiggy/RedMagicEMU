#!/usr/bin/env python3
"""Exercise generated swapchain create request and response statements offline."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('generated', type=Path)
args = parser.parse_args()
guest = (args.generated / 'guest/VkEncoder.cpp').read_text()
host = (args.generated / 'host/VkDecoder.cpp').read_text()
begin = guest.index('VkEncoder::vkCreateSwapchainKHR(')
end = guest.index('VkEncoder::vkDestroySwapchainKHR(', begin)
guest = guest[begin:end]
watchdog = guest.index('  if (watchdog) {', guest.index('  uint32_t opcode_vkCreateSwapchainKHR'))
request_end = guest.rindex('  /* is handle, possibly out */;', 0, watchdog)
request_start = guest.rindex('  /* is handle, possibly out */;', 0, request_end)
request = guest[request_start:watchdog]
read_start = guest.find('  uint64_t rmeSwapchainWire', watchdog)
if read_start < 0:
    read_start = guest.index('  stream->setHandleMapping(sResourceTracker->createMapping());', watchdog)
read_end = guest.index('  ++encodeCount;', read_start)
guest_response = guest[read_start:read_end]
begin = host.index('case OP_vkCreateSwapchainKHR:')
end = host.index('case OP_', begin + 8)
host = host[begin:end]
start = host.index('      vkStream->unsetHandleMapping();', host.index('m_state->on_CheckOutOfMemory'))
guard = host.find('if (vkCreateSwapchainKHR_VkResult_return != VK_SUCCESS)',
                  host.index('m_state->on_CheckOutOfMemory'), start)
if guard >= 0:
    start = host.rfind('\n', 0, guard) + 1
end = host.index('      vkStream->commitWrite();', start)
host_response = host[start:end]

program = r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
using VkResult = int32_t;
using VkSwapchainKHR = uint64_t;
constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_ERROR_INITIALIZATION_FAILED = -3;
constexpr VkResult VK_ERROR_DEVICE_LOST = -4;
constexpr VkSwapchainKHR VK_NULL_HANDLE = 0;
static int maps = 0, records = 0;
struct Mapping {
    bool create;
    void mapHandles_VkSwapchainKHR(VkSwapchainKHR* value, int) {
        if (create) { ++maps; *value += 0x100; }
    }
    void mapHandles_u64_VkSwapchainKHR(uint64_t* value, VkSwapchainKHR* out, int) {
        if (create) ++maps;
        *out = *value + 0x1000;
    }
} m_boxedHandleCreateMapping{true}, m_boxedHandleUnwrapMapping{false}, ordinary{false}, guestCreate{true};
struct Tracker { Mapping* createMapping() { return &guestCreate; } } tracker;
static Tracker* sResourceTracker = &tracker;
struct State { void on_recordSwapchain(int, VkSwapchainKHR, const void*) { ++records; } } state;
struct Wire {
    uint8_t bytes[32]{}; unsigned pos = 0, size = 0; Mapping* mapping = &ordinary;
    void setHandleMapping(Mapping* value) { mapping = value; }
    void unsetHandleMapping() { mapping = &ordinary; }
    Mapping* handleMapping() { return mapping; }
    void write(const void* p, unsigned n) { memcpy(bytes + size, p, n); size += n; }
    void read(void* p, unsigned n) { memcpy(p, bytes + pos, n); pos += n; }
};
static uint64_t request(VkSwapchainKHR* pSwapchain) {
    uint8_t out[8]{}; uint8_t* ptr = out; uint8_t** streamPtrPtr = &ptr;
''' + request + r'''
    uint64_t result; memcpy(&result, out, sizeof(result)); return result;
}
static Wire hostReply(VkResult vkCreateSwapchainKHR_VkResult_return, VkSwapchainKHR raw) {
    VkSwapchainKHR* pSwapchain = &raw; Wire wire; Wire* vkStream = &wire;
    State* m_state = &state; int unboxed_device = 1; const void* pCreateInfo = nullptr;
''' + host_response + r'''
    return wire;
}
static VkResult guestReply(Wire* stream, VkSwapchainKHR* pSwapchain) {
''' + guest_response + r'''
    return vkCreateSwapchainKHR_VkResult_return;
}
int main(int argc, char** argv) {
    if (argc != 2) return 99;
    if (!strcmp(argv[1], "request")) {
        VkSwapchainKHR out = 0xdeadbeef;
        return request(&out) == 0 && out == 0xdeadbeef ? 0 : 1;
    }
    if (!strcmp(argv[1], "host")) {
        maps = records = 0; Wire failed = hostReply(VK_ERROR_DEVICE_LOST, 0x1234);
        uint64_t wire = 1; memcpy(&wire, failed.bytes, 8);
        if (wire || maps || records) return 2;
        Wire success = hostReply(VK_SUCCESS, 0x2345);
        memcpy(&wire, success.bytes, 8);
        return wire == 0x2445 && maps == 1 && records == 1 ? 0 : 3;
    }
    if (!strcmp(argv[1], "guest")) {
        maps = 0; VkSwapchainKHR output = 0xdead;
        Wire failed; uint64_t raw = 0x1234; VkResult error = VK_ERROR_DEVICE_LOST;
        failed.write(&raw, 8); failed.write(&error, sizeof(error));
        if (guestReply(&failed, &output) != error || output || maps) return 4;
        Wire success; uint64_t boxed = 0x2345; VkResult ok = VK_SUCCESS;
        success.write(&boxed, 8); success.write(&ok, sizeof(ok)); output = 0;
        if (guestReply(&success, &output) != VK_SUCCESS || output != 0x3345 || maps != 1) return 5;
        Wire bad; boxed = 0; bad.write(&boxed, 8); bad.write(&ok, sizeof(ok)); output = 0xdead;
        return guestReply(&bad, &output) == VK_ERROR_INITIALIZATION_FAILED && !output && maps == 1 ? 0 : 6;
    }
    return 98;
}
'''
with tempfile.TemporaryDirectory(prefix='rme-swapchain-create-test-') as tmp:
    root = Path(tmp)
    cpp = root / 'test.cpp'
    cpp.write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                    '-Werror', '-Wno-unused-parameter', '-Wno-unused-variable',
                    str(cpp), '-o', str(root / 'test')], check=True)
    failures = []
    for case in ('request', 'host', 'guest'):
        result = subprocess.run([str(root / 'test'), case])
        print(case.upper(), 'PASS' if result.returncode == 0 else 'FAIL', result.returncode, flush=True)
        if result.returncode:
            failures.append(case)
    if failures:
        raise SystemExit('swapchain create failures: ' + ', '.join(failures))
print('SWAPCHAIN_CREATE_WIRE_SUCCESS_FAILURE_OK')
