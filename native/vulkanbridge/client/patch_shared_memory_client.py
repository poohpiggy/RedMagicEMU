#!/usr/bin/env python3
"""Build the shared-memory tracker and encoder for the paired EMU client."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

original, output = map(Path, sys.argv[1:])
source = original / "patched-source/guest/vulkan_enc"
patched = output / "patched-source/guest/vulkan_enc"
shutil.copytree(source, patched)


def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError("unexpected client source near " + old[:80])
    return text.replace(old, new, 1)


header = (source / "ResourceTracker.h").read_text()
header = replace(header, "    bool usingDirectMapping() const;",
                 """    bool usingDirectMapping() const;
    bool syncSharedMemory(VkDevice device, bool cpuAccess);
    bool syncSharedQueueMemory(VkQueue queue, bool cpuAccess);
    bool normalizeCachedRanges(uint32_t count, const VkMappedMemoryRange* ranges,
                               std::vector<VkMappedMemoryRange>& normalized);""")
header = replace(header, "    struct VkDeviceMemory_Info {",
                 """    struct RmeSharedMapping;
    struct VkDeviceMemory_Info {
        std::shared_ptr<RmeSharedMapping> rmeMapping;""")
(patched / "ResourceTracker.h").write_text(header)

tracker = (source / "ResourceTracker.cpp").read_text()
tracker = replace(tracker, '#include "ResourceTracker.h"', '''#include "ResourceTracker.h"
#include <cerrno>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

extern "C" int rme_vulkan_memory_fd(uint64_t memory, int* cached);
''')
tracker = replace(tracker, "bool ResourceTracker::usingDirectMapping() const { return true; }",
                  """bool ResourceTracker::usingDirectMapping() const { return true; }

struct ResourceTracker::RmeSharedMapping {
    const int fd;
    const bool cached;
    void* base = MAP_FAILED;
    size_t length = 0;
    VkDeviceSize mapBegin = 0;
    VkDeviceSize mapEnd = 0;
    bool cpuAccess = false;

    RmeSharedMapping(int descriptor, bool useCache) : fd(descriptor), cached(useCache) {}
    ~RmeSharedMapping() {
        unmap();
        close(fd);
    }

    bool sync(bool begin) {
        if (cached || base == MAP_FAILED || begin == cpuAccess) return true;
        dma_buf_sync operation{};
        operation.flags = DMA_BUF_SYNC_RW | (begin ? DMA_BUF_SYNC_START : DMA_BUF_SYNC_END);
        int result;
        do {
            result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &operation);
        } while (result < 0 && (errno == EINTR || errno == EAGAIN));
        if (result == 0) cpuAccess = begin;
        return result == 0;
    }

    void unmap() {
        if (base != MAP_FAILED) {
            sync(false);
            munmap(base, length);
            base = MAP_FAILED;
            cpuAccess = false;
        }
    }
};

bool ResourceTracker::syncSharedMemory(VkDevice device, bool cpuAccess) {
    AutoLock<RecursiveLock> lock(mLock);
    for (const auto& entry : info_VkDeviceMemory) {
        const auto& info = entry.second;
        if (info.device == device && info.rmeMapping && !info.rmeMapping->sync(cpuAccess)) {
            return false;
        }
    }
    return true;
}

bool ResourceTracker::syncSharedQueueMemory(VkQueue queue, bool cpuAccess) {
    AutoLock<RecursiveLock> lock(mLock);
    const auto info = info_VkQueue.find(queue);
    return info != info_VkQueue.end() && syncSharedMemory(info->second.device, cpuAccess);
}

bool ResourceTracker::normalizeCachedRanges(uint32_t count, const VkMappedMemoryRange* ranges,
                                             std::vector<VkMappedMemoryRange>& normalized) {
    AutoLock<RecursiveLock> lock(mLock);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& range = ranges[i];
        const auto info = info_VkDeviceMemory.find(range.memory);
        if (info == info_VkDeviceMemory.end()) continue;
        const auto& mapping = info->second.rmeMapping;
        if (!mapping || !mapping->cached) continue;
        if (mapping->base == MAP_FAILED || range.offset < mapping->mapBegin ||
            range.offset >= mapping->mapEnd) return false;
        const auto remaining = mapping->mapEnd - range.offset;
        if (range.size == VK_WHOLE_SIZE) {
            if (normalized.empty()) normalized.assign(ranges, ranges + count);
            normalized[i].size = remaining;
        } else if (range.size > remaining) {
            return false;
        }
    }
    return true;
}""")
tracker = replace(tracker, """    // Host visible memory with direct mapping
    VkResult result = getCoherentMemory(&finalAllocInfo, enc, device, pMemory);""",
                  """    VkResult result = enc->vkAllocateMemory(device, &finalAllocInfo, pAllocator,
                                            pMemory, true);
    if (result != VK_SUCCESS) _RETURN_FAILURE_WITH_DEVICE_MEMORY_REPORT(result);
    int cached = 0;
    const int fd = rme_vulkan_memory_fd(get_host_u64_VkDeviceMemory(*pMemory), &cached);
    if (fd < 0) {
        enc->vkFreeMemory(device, *pMemory, pAllocator, true);
        *pMemory = VK_NULL_HANDLE;
        _RETURN_FAILURE_WITH_DEVICE_MEMORY_REPORT(VK_ERROR_MEMORY_MAP_FAILED);
    }
    {
        AutoLock<RecursiveLock> lock(mLock);
        setDeviceMemoryInfo(device, *pMemory, finalAllocInfo.allocationSize, nullptr,
                            finalAllocInfo.memoryTypeIndex, nullptr, false, ZX_HANDLE_INVALID, nullptr);
        info_VkDeviceMemory[*pMemory].rmeMapping = std::make_shared<RmeSharedMapping>(fd, cached != 0);
    }""")
tracker = replace(tracker, "    auto& deviceMemoryInfo = deviceMemoryInfoIt->second;",
                  """    auto& deviceMemoryInfo = deviceMemoryInfoIt->second;
    if (auto& shared = deviceMemoryInfo.rmeMapping; shared) {
        const auto length = deviceMemoryInfo.allocationSize;
        if (shared->base != MAP_FAILED || offset >= length ||
            (size != VK_WHOLE_SIZE && size > length - offset)) {
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        shared->length = length;
        shared->base = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, shared->fd, 0);
        if (shared->base == MAP_FAILED || !shared->sync(true)) {
            shared->unmap();
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        shared->mapBegin = offset;
        shared->mapEnd = size == VK_WHOLE_SIZE ? length : offset + size;
        deviceMemoryInfo.ptr = static_cast<uint8_t*>(shared->base);
        *ppData = deviceMemoryInfo.ptr + offset;
        return VK_SUCCESS;
    }""")
tracker = replace(tracker, """void ResourceTracker::on_vkUnmapMemory(void*, VkDevice, VkDeviceMemory) {
    // no-op
}""", """void ResourceTracker::on_vkUnmapMemory(void*, VkDevice, VkDeviceMemory memory) {
    AutoLock<RecursiveLock> lock(mLock);
    const auto info = info_VkDeviceMemory.find(memory);
    if (info != info_VkDeviceMemory.end() && info->second.rmeMapping) {
        info->second.rmeMapping->unmap();
        info->second.ptr = nullptr;
    }
}""")
(patched / "ResourceTracker.cpp").write_text(tracker)

encoder = (source / "VkEncoder.cpp").read_text()
for name in ("vkFlushMappedMemoryRanges", "vkInvalidateMappedMemoryRanges"):
    start = encoder.index("VkResult VkEncoder::" + name + "(")
    body = encoder.index("{\n", start) + 2
    encoder = (encoder[:body] + """    std::vector<VkMappedMemoryRange> normalized;
    if (!sResourceTracker->normalizeCachedRanges(memoryRangeCount, pMemoryRanges, normalized))
        return VK_ERROR_MEMORY_MAP_FAILED;
    if (!normalized.empty()) pMemoryRanges = normalized.data();
""" + encoder[body:])
for name, handle, operation in (
    ("vkQueueSubmit", "queue", "syncSharedQueueMemory"),
    ("vkQueueSubmit2", "queue", "syncSharedQueueMemory"),
    ("vkFlushMappedMemoryRanges", "device", "syncSharedMemory"),
):
    start = encoder.index("VkResult VkEncoder::" + name + "(")
    body = encoder.index("{\n", start) + 2
    encoder = (encoder[:body]
               + f"    if (!sResourceTracker->{operation}({handle}, false)) return VK_ERROR_MEMORY_MAP_FAILED;\n"
               + encoder[body:])

for name, handle, operation in (
    ("vkQueueWaitIdle", "queue", "syncSharedQueueMemory"),
    ("vkDeviceWaitIdle", "device", "syncSharedMemory"),
    ("vkWaitForFences", "device", "syncSharedMemory"),
    ("vkGetFenceStatus", "device", "syncSharedMemory"),
    ("vkWaitSemaphores", "device", "syncSharedMemory"),
    ("vkInvalidateMappedMemoryRanges", "device", "syncSharedMemory"),
):
    result = name + "_VkResult_return"
    encoder = replace(encoder, f"    return {result};",
                      f"""    if ({result} == VK_SUCCESS && !sResourceTracker->{operation}({handle}, true)) {{
        return VK_ERROR_MEMORY_MAP_FAILED;
    }}
    return {result};""")
(patched / "VkEncoder.cpp").write_text(encoder)

if os.environ.get("RME_VULKAN_SCOPED_DMA_SYNC") == "1":
    from patch_scoped_dma_sync import apply

    apply(patched)

for name in ("ResourceTracker", "VkEncoder"):
    cmd = shlex.split((original / f"android1-{name}.command.txt").read_text())
    cmd = [arg.replace(str(source), str(patched)) for arg in cmd]
    for flag, suffix in (("-o", ".o"), ("-MF", ".d")):
        cmd[cmd.index(flag) + 1] = str(output / (f"android1-{name}" + suffix))
    subprocess.run(cmd, check=True)
