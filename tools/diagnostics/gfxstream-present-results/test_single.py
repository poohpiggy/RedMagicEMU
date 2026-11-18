#!/usr/bin/env python3
"""Check original wire is unchanged and execute the generated guest guard."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def body(text, start, end):
    text = text[text.index(start):]
    return text[:text.index(end, len(start))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    get = lambda p, name: (p / name).read_text()
    start, end = "VkResult VkEncoder::vkQueuePresentKHR(", "VkResult VkEncoder::"
    base = body(get(args.baseline, "guest/VkEncoder.cpp"), start, end)
    candidate = body(get(args.candidate, "guest/VkEncoder.cpp"), start, end)
    guard = re.search(r"  if \(pPresentInfo->swapchainCount != 1 \|\| pPresentInfo->pResults\) \{.*?\n  \}\n", candidate, re.S)
    if not guard:
        raise ValueError("generated single-present guard is absent")
    if candidate[:guard.start()] + candidate[guard.end():] != base:
        raise ValueError("guest function differs beyond the guard")
    start, end = "case OP_vkQueuePresentKHR:", "case OP_"
    if body(get(args.baseline, "host/VkDecoder.cpp"), start, end) != body(get(args.candidate, "host/VkDecoder.cpp"), start, end):
        raise ValueError("host present decoder changed")
    program = r'''
#include <cassert>
#include <cstdint>
using VkResult = int32_t;
constexpr VkResult VK_ERROR_FEATURE_NOT_PRESENT = -8;
struct VkPresentInfoKHR { uint32_t swapchainCount; VkResult* pResults; };
static int forwarded = 0;
static VkResult present(const VkPresentInfoKHR* pPresentInfo) {
''' + guard.group(0) + r'''
    ++forwarded; return 123;
}
int main() {
    VkPresentInfoKHR single{1, nullptr};
    assert(present(&single) == 123 && forwarded == 1);
    VkPresentInfoKHR multi{2, nullptr};
    assert(present(&multi) == VK_ERROR_FEATURE_NOT_PRESENT && forwarded == 1);
    VkResult output[] = {99, 99, 99};
    VkPresentInfoKHR withResults{2, output};
    assert(present(&withResults) == VK_ERROR_FEATURE_NOT_PRESENT && forwarded == 1);
    assert(output[0] == VK_ERROR_FEATURE_NOT_PRESENT && output[1] == VK_ERROR_FEATURE_NOT_PRESENT && output[2] == 99);
}
'''
    with tempfile.TemporaryDirectory(prefix="rme-present-single-") as tmp:
        root = Path(tmp)
        source = root / "test.cpp"
        source.write_text(program)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        *shlex.split(os.environ.get("CXXFLAGS", "")), str(source), "-o", str(root / "test"),
                        *shlex.split(os.environ.get("LDFLAGS", ""))], check=True)
        subprocess.run([str(root / "test")], check=True)
    print("PASS: guard rejects unsupported cases; accepted guest and host code retain original wire")


if __name__ == "__main__":
    main()
