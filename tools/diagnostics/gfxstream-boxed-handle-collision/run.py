#!/usr/bin/env python3
"""Exercise d7a's real boxed-manager methods with an in-memory fake store.

This CPU-only diagnostic executes no graphics, devices, guest code, or mapping.
It tests removal, update and delayed removal against reverse-map collisions.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--patch", type=Path)
    parser.add_argument("--type-patch", type=Path)
    parser.add_argument("--expect-typed", action="store_true")
    parser.add_argument("--real-store", action="store_true")
    args = parser.parse_args()
    relative = Path("host/vulkan/VkDecoderGlobalState.cpp")
    original = (args.source / relative).read_text()
    with tempfile.TemporaryDirectory(prefix="rme-boxed-source-") as temp:
        root = Path(temp)
        target = root / relative
        target.parent.mkdir(parents=True)
        target.write_text(original)
        if args.patch:
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(args.patch.resolve())], cwd=root, check=True)
        if args.type_patch:
            subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(args.type_patch.resolve())], cwd=root, check=True)
        text = target.read_text()
    start = text.index("template <class T>\nclass BoxedHandleManager {")
    end = text.index("\nstruct OrderMaintenanceInfo {", start)
    manager = text[start:end]
    program = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_map>
#include <vector>
using VkDevice = uintptr_t;
#ifdef RME_REAL_STORE
#include "aemu/base/containers/HybridEntityManager.h"
using android::base::Lock;
using android::base::AutoLock;
#else
struct Lock {};
struct AutoLock { explicit AutoLock(Lock&) {} };
#endif
enum BoxedHandleTypeTag { Image = 1, Other = 2 };
#ifndef RME_REAL_STORE
namespace android { namespace base {
template <class Map, class Key> auto find(Map& map, Key key) -> typename Map::mapped_type* {
    auto it = map.find(key); return it == map.end() ? nullptr : &it->second;
}
template <size_t N, class Key, class T> class HybridEntityManager {
    std::unordered_map<Key, T> entries;
    Key next = 1;
public:
    struct EM { static size_t getHandleType(Key id) { return id >> 48; } };
    Key add(const T& item, size_t type) { const Key id = (uint64_t(type) << 48) | (uint64_t(1) << 32) | next++; entries[id] = item; return id; }
    Key addFixed(Key id, const T& item, size_t) { entries[id] = item; return id; }
    T* get(Key id) { auto it = entries.find(id); return it == entries.end() ? nullptr : &it->second; }
    const T* get_const(Key id) const { auto it = entries.find(id); return it == entries.end() ? nullptr : &it->second; }
    void remove(Key id) { entries.erase(id); }
    void clear() { entries.clear(); }
};
}}
#endif
''' + manager + r'''
struct Item { uint64_t underlying; };
#ifdef RME_TYPED_KEY
template<class M> auto typedReverse(M& m, uint64_t value, BoxedHandleTypeTag tag, int)
    -> decltype(m.getBoxedFromUnboxedLocked(value, tag)) {
    return m.getBoxedFromUnboxedLocked(value, tag);
}
template<class M> uint64_t typedReverse(M& m, uint64_t value, BoxedHandleTypeTag, long) {
    return m.getBoxedFromUnboxedLocked(value);
}
#define REV(m, value, tag) typedReverse((m), (value), (tag), 0)
#else
#define REV(m, value, tag) (m).getBoxedFromUnboxedLocked((value))
#endif
int main() {
    {
        BoxedHandleManager<Item> manager;
        const auto first = manager.add({0x1234}, Image);
        const auto second = manager.add({0x1234}, Image);
        manager.remove(first);
        if (REV(manager, 0x1234, Image) != second || !manager.get(second)) return 1;
        manager.remove(second);
        if (REV(manager, 0x1234, Image)) return 2;
    }
    {
        BoxedHandleManager<Item> manager;
        const auto first = manager.add({0x1234}, Image);
        const auto second = manager.add({0x1234}, Image);
        manager.update(first, {0x5678}, Image);
        if (REV(manager, 0x1234, Image) != second || REV(manager, 0x5678, Image) != first) return 3;
    }
    {
        BoxedHandleManager<Item> manager;
        const auto first = manager.add({0x1234}, Image);
        const auto second = manager.add({0x1234}, Image);
        manager.removeDelayed(first, 42, nullptr);
        manager.processDelayedRemovesGlobalStateLocked(42);
        if (manager.get(first) || REV(manager, 0x1234, Image) != second) return 4;
        manager.removeDelayed(second, 42, nullptr);
        manager.processDelayedRemovesGlobalStateLocked(42);
        if (manager.get(second) || REV(manager, 0x1234, Image)) return 5;
    }
#ifdef RME_TYPED_KEY
    {
        BoxedHandleManager<Item> manager;
        const auto image = manager.add({0x1234}, Image);
        const auto other = manager.add({0x1234}, Other);
        if (REV(manager, 0x1234, Image) != image || REV(manager, 0x1234, Other) != other) return 6;
        manager.remove(image);
        if (REV(manager, 0x1234, Image) || REV(manager, 0x1234, Other) != other) return 7;
    }
    {
        BoxedHandleManager<Item> manager;
        const auto fixed = manager.addFixed((uint64_t(Image) << 48) | (uint64_t(1) << 32) | 42, {0x1234}, Image);
        const auto other = manager.add({0x1234}, Other);
        if (REV(manager, 0x1234, Image) != fixed || REV(manager, 0x1234, Other) != other) return 8;
        manager.update(fixed, {0x5678}, Image);
        if (REV(manager, 0x5678, Image) != fixed || REV(manager, 0x1234, Other) != other) return 9;
        manager.removeDelayed(fixed, 42, nullptr);
        manager.processDelayedRemovesGlobalStateLocked(42);
        if (REV(manager, 0x5678, Image) || REV(manager, 0x1234, Other) != other) return 10;
    }
#endif
    puts("PASS: reverse mapping survives colliding removal/update and is cleared after delayed deletion");
}
'''
    with tempfile.TemporaryDirectory(prefix="rme-boxed-collision-") as temp:
        root = Path(temp)
        cpp = root / "test.cpp"
        cpp.write_text(program)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                        *(["-DRME_TYPED_KEY"] if args.type_patch or args.expect_typed else []),
                        *(["-DRME_REAL_STORE"] if args.real_store else []),
                        *shlex.split(os.environ.get("CXXFLAGS", "")), str(cpp),
                        "-o", str(root / "test"),
                        *shlex.split(os.environ.get("LDFLAGS", ""))], check=True)
        subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    main()
