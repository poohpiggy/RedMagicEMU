#!/usr/bin/env python3
"""Execute the generated support-query output encoding against a sentinel."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("encoder", type=Path)
args = parser.parse_args()
text = args.encoder.read_text()
start = text.index("VkResult VkEncoder::vkGetPhysicalDeviceSurfaceSupportKHR(")
end = text.index("  if (watchdog) {", start)
body = text[start:end]
matches = re.findall(r"(?:memcpy|memset)\(\*streamPtrPtr,[^;]*sizeof\(VkBool32\)\);", body, re.S)
if len(matches) != 1:
    raise SystemExit("could not identify the support-output request write")
program = r'''
#include <stdint.h>
#include <string.h>
typedef uint32_t VkBool32;
int main(void) {
    VkBool32 sentinel = 0x5a5a5a5a;
    VkBool32* pSupported = &sentinel;
    (void)pSupported;
    uint8_t buffer[sizeof(VkBool32)] = {};
    uint8_t* streamPtr = buffer;
    uint8_t** streamPtrPtr = &streamPtr;
''' + matches[0] + r'''
    VkBool32 encoded = 0xffffffff;
    memcpy(&encoded, buffer, sizeof(encoded));
    return encoded == 0 && sentinel == 0x5a5a5a5a ? 0 : 1;
}
'''
with tempfile.TemporaryDirectory(prefix="rme-surface-support-output-") as tmp:
    root = Path(tmp)
    source = root / "test.cpp"
    source.write_text(program)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-nostdlib++", str(source), "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True, cwd=root)
print("SUPPORT_QUERY_OUTPUT_REQUEST_ZEROED")
