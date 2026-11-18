#!/usr/bin/env python3
"""Test response statements extracted from actual generated encoder/decoder."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def extract(text, start, stop, pattern):
    body = text[text.index(start) + len(start):]
    body = body[:body.index(stop)]
    match = re.search(pattern, body, re.S)
    if not match:
        raise ValueError("missing generated response: " + pattern)
    return match.group(0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("generated", type=Path)
    args = parser.parse_args()
    host = (args.generated / "host/VkDecoder.cpp").read_text()
    guest = (args.generated / "guest/VkEncoder.cpp").read_text()
    host_write = extract(host, "case OP_vkQueuePresentKHR:", "case OP_",
                         r"if\s*\(pPresentInfo\s*&&\s*pPresentInfo->pResults\)\s*vkStream->write\(pPresentInfo->pResults,\s*pPresentInfo->swapchainCount\s*\*\s*sizeof\(VkResult\)\);")
    guest_read = extract(guest, "VkResult VkEncoder::vkQueuePresentKHR(", "VkResult VkEncoder::",
                         r"if\s*\(rmeOriginalResults\)\s*stream->read\(rmeOriginalResults,\s*pPresentInfo->swapchainCount\s*\*\s*sizeof\(VkResult\)\);")
    guest_prepare = extract(guest, "VkResult VkEncoder::vkQueuePresentKHR(", "VkResult VkEncoder::",
                            r"VkResult \*rmeOriginalResults = pPresentInfo->pResults;.*?pPresentInfo = &rmePresentInfo;")
    program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
using VkResult = int32_t;
constexpr VkResult VK_SUCCESS = 0;
struct VkPresentInfoKHR { uint32_t swapchainCount; VkResult* pResults; };
struct Wire {
    std::vector<unsigned char> bytes;
    size_t offset = 0;
    void write(const void* p, size_t n) { auto b = static_cast<const unsigned char*>(p); bytes.insert(bytes.end(), b, b+n); }
    void read(void* p, size_t n) { assert(offset+n <= bytes.size()); memcpy(p, bytes.data()+offset, n); offset+=n; }
};
static void host(VkPresentInfoKHR* pPresentInfo, Wire* vkStream, VkResult result) {
    {
''' + host_write + r'''
    }
    vkStream->write(&result, sizeof(result));
}
static VkResult guest(const VkPresentInfoKHR* pPresentInfo, Wire* stream) {
''' + guest_prepare + r'''
    if (rmeOriginalResults) {
        assert(rmePresentInfo.pResults != rmeOriginalResults);
        for (uint32_t i=0; i<pPresentInfo->swapchainCount; ++i) assert(rmePresentInfo.pResults[i] == VK_SUCCESS);
    }
    {
''' + guest_read + r'''
    }
    VkResult result = VK_SUCCESS;
    stream->read(&result, sizeof(result));
    return result;
}
int main() {
    VkResult produced[] = { 41, -42 };
    VkResult received[] = { 99, 99, 99 };
    VkPresentInfoKHR hp{2, produced}, gp{2, received}; Wire w;
    host(&hp, &w, -7); assert(guest(&gp, &w) == -7);
    assert(received[0] == 41 && received[1] == -42 && received[2] == 99 && w.offset == w.bytes.size());
    VkPresentInfoKHR nullp{2, nullptr}; Wire z;
    host(&nullp, &z, -8); assert(guest(&nullp, &z) == -8 && z.offset == z.bytes.size());
}
'''
    with tempfile.TemporaryDirectory(prefix="rme-present-wire-") as temp:
        root = Path(temp)
        cpp = root / "test.cpp"
        cpp.write_text(program)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *shlex.split(os.environ.get("CXXFLAGS", "")), str(cpp), "-o", str(root / "test"),
                        *shlex.split(os.environ.get("LDFLAGS", ""))], check=True)
        subprocess.run([str(root / "test")], check=True)
    print("PASS: generated host/guest preserve per-swapchain results, overall result and null pResults")


if __name__ == "__main__":
    main()
