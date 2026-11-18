#!/usr/bin/env python3
"""Generate a type-keyed reverse map patch atop the prior cleanup patch."""
import argparse
import difflib
from pathlib import Path
import subprocess
import tempfile


def replace(text, old, new, count=1):
    if text.count(old) != count:
        raise ValueError(f"unexpected occurrence count for {old[:70]!r}: {text.count(old)}")
    return text.replace(old, new)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    relative = Path("host/vulkan/VkDecoderGlobalState.cpp")
    with tempfile.TemporaryDirectory(prefix="rme-typed-patch-") as temp:
        root = Path(temp)
        target = root / relative
        target.parent.mkdir(parents=True)
        target.write_bytes((args.source / relative).read_bytes())
        cleanup = Path(__file__).resolve().with_name("fix.patch")
        subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(cleanup)], cwd=root, check=True)
        original = target.read_text()
    text = original
    text = replace(text, '    using Store = android::base::HybridEntityManager<16000, uint64_t, T>;\n',
                   '    using Store = android::base::HybridEntityManager<16000, uint64_t, T>;\n'
                   '    using HandleCodec = typename Store::EM;\n')
    text = replace(text, '    std::unordered_map<uint64_t, uint64_t> reverseMap;\n',
                   '    std::unordered_map<uint64_t, std::unordered_map<size_t, uint64_t>> reverseMap;\n')
    text = replace(text, '        reverseMap[(uint64_t)(item.underlying)] = res;\n',
                   '        reverseMap[(uint64_t)(item.underlying)][HandleCodec::getHandleType(res)] = res;\n', 2)
    text = replace(text, '        reverseMap[(uint64_t)(item.underlying)] = handle;\n',
                   '        reverseMap[(uint64_t)(item.underlying)][HandleCodec::getHandleType(handle)] = handle;\n')
    text = replace(text, '''        auto it = reverseMap.find(underlying);
        if (it != reverseMap.end() && it->second == boxed) reverseMap.erase(it);
''', '''        auto it = reverseMap.find(underlying);
        if (it == reverseMap.end()) return;
        auto typed = it->second.find(HandleCodec::getHandleType(boxed));
        if (typed != it->second.end() && typed->second == boxed) it->second.erase(typed);
        if (it->second.empty()) reverseMap.erase(it);
''')
    text = replace(text, '''    uint64_t getBoxedFromUnboxedLocked(uint64_t unboxed) {
        auto* res = android::base::find(reverseMap, unboxed);
        if (!res) return 0;
        return *res;
    }
''', '''    uint64_t getBoxedFromUnboxedLocked(uint64_t unboxed, BoxedHandleTypeTag type) {
        auto it = reverseMap.find(unboxed);
        if (it == reverseMap.end()) return 0;
        auto typed = it->second.find((size_t)type);
        return typed == it->second.end() ? 0 : typed->second;
    }
''')
    text = replace(text,
                   'sBoxedHandleManager.getBoxedFromUnboxedLocked((uint64_t)(uintptr_t)unboxed)',
                   'sBoxedHandleManager.getBoxedFromUnboxedLocked((uint64_t)(uintptr_t)unboxed, Tag_##type)', 4)
    patch = difflib.unified_diff(original.splitlines(keepends=True), text.splitlines(keepends=True),
                                 fromfile="a/" + str(relative), tofile="b/" + str(relative))
    args.output.write_text("".join(patch))


if __name__ == "__main__":
    main()
