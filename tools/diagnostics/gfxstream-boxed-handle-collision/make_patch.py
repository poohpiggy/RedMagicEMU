#!/usr/bin/env python3
"""Recreate the narrow d7a reverse-map cleanup patch."""
import argparse
import difflib
from pathlib import Path


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError("unexpected source layout: " + old[:70])
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    relative = "host/vulkan/VkDecoderGlobalState.cpp"
    original = (args.source / relative).read_text()
    text = original
    marker = '''    void update(uint64_t handle, const T& item, BoxedHandleTypeTag tag) {
'''
    helper = '''    // The caller must hold lock. A different live box may have replaced
    // this reverse-map entry after sharing the same underlying value.
    void eraseReverseMappingIfMatches(uint64_t underlying, uint64_t boxed) {
        auto it = reverseMap.find(underlying);
        if (it != reverseMap.end() && it->second == boxed) reverseMap.erase(it);
    }

'''
    text = replace_once(text, marker, helper + marker)
    text = replace_once(text, '''        if (oldHandle) {
            reverseMap.erase(oldHandle);
        }
''', '''        if (oldHandle) {
            eraseReverseMappingIfMatches(oldHandle, handle);
        }
''')
    text = replace_once(text, '''            reverseMap.erase((uint64_t)(item->underlying));
''', '''            eraseReverseMappingIfMatches((uint64_t)(item->underlying), h);
''')
    text = replace_once(text, '''            store.remove(h);
        }
        delayedRemovesList.clear();
''', '''            auto item = get(h);
            if (item) eraseReverseMappingIfMatches((uint64_t)(item->underlying), h);
            store.remove(h);
        }
        delayedRemovesList.clear();
''')
    diff = difflib.unified_diff(original.splitlines(keepends=True), text.splitlines(keepends=True),
                                fromfile="a/" + relative, tofile="b/" + relative)
    args.output.write_text("".join(diff))


if __name__ == "__main__":
    main()
