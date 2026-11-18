#!/usr/bin/env python3
"""Test generated swapchain-image request with output buffers as sentinels."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('generated', type=Path)
args = parser.parse_args()
source = (args.generated / 'guest/VkEncoder.cpp').read_text()
start = source.index('VkEncoder::vkGetSwapchainImagesKHR(')
end = source.index('VkEncoder::vkAcquireNextImageKHR(', start)
body = source[start:end]
start = body.find('  /* RME output count */;', body.index('get_host_u64_VkSwapchainKHR'))
if start < 0:
    start = body.index('  // WARNING PTR CHECK', body.index('get_host_u64_VkSwapchainKHR'))
end = body.index('  if (watchdog) {', start)
request = body[start:end]
program = r'''
#include <cstdint>
#include <cstring>
using VkImage = uint64_t;
namespace gfxstream { namespace guest { struct Stream { static void toBe64(uint8_t*) {} }; } }
static int exercise(bool imageArray) {
    uint32_t original = imageArray ? 2 : 0xdeadbeef;
    uint32_t* pSwapchainImageCount = &original;
    VkImage images[2] = {0xabcdef, 0x123456};
    VkImage* pSwapchainImages = imageArray ? images : nullptr;
    uint8_t bytes[64]{}; uint8_t* position = bytes; uint8_t** streamPtrPtr = &position;
''' + request + r'''
    uint32_t sentCount; memcpy(&sentCount, bytes + 8, 4);
    if (sentCount != (imageArray ? 2u : 0u)) return 1;
    if (original != (imageArray ? 2u : 0xdeadbeefu)) return 2;
    if (imageArray) {
        uint64_t first, second; memcpy(&first, bytes + 20, 8); memcpy(&second, bytes + 28, 8);
        if (first || second || images[0] != 0xabcdef || images[1] != 0x123456) return 3;
    }
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 2) return 99;
    return exercise(!strcmp(argv[1], "array"));
}
'''
with tempfile.TemporaryDirectory(prefix='rme-images-request-') as tmp:
    root = Path(tmp)
    cpp = root / 'test.cpp'
    cpp.write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(cpp), '-o', str(root / 'test')], check=True)
    failures = []
    for case in ('count', 'array'):
        result = subprocess.run([str(root / 'test'), case])
        print(case.upper(), 'PASS' if result.returncode == 0 else 'FAIL', result.returncode, flush=True)
        if result.returncode:
            failures.append(case)
    if failures:
        raise SystemExit('swapchain image request failures: ' + ', '.join(failures))
print('SWAPCHAIN_IMAGES_REQUEST_OUTPUT_ONLY_OK')
