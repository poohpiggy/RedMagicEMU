#!/usr/bin/env python3
"""Limit DMA-BUF cache transitions for submissions with known resource access."""

import re
import sys
from pathlib import Path


def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError("unexpected encoder source near " + old[:80])
    return text.replace(old, new, 1)


def apply(directory, trace=False):
    header = directory / "ResourceTracker.h"
    tracker = directory / "ResourceTracker.cpp"
    encoder = directory / "VkEncoder.cpp"
    h = replace(header.read_text(), "    bool usingDirectMapping() const;", """    bool usingDirectMapping() const;
    void beginSharedCommands(VkCommandBuffer command);
    void recordSharedCommand(VkCommandBuffer command, std::initializer_list<VkBuffer> buffers,
                             std::initializer_list<VkImage> images);
    void recordSharedBarrier(VkCommandBuffer command, uint32_t bufferCount,
                             const VkBufferMemoryBarrier* buffers, uint32_t imageCount,
                             const VkImageMemoryBarrier* images);
    void invalidateSharedCommands(VkCommandBuffer command);
    bool syncSharedSubmission(VkQueue queue, uint32_t count, const VkSubmitInfo* submits);""")
    h = replace(h, "    struct VkCommandBuffer_Info {", """    struct VkCommandBuffer_Info {
        bool sharedCommandsKnown = false;
        std::vector<VkBuffer> sharedBuffers;
        std::vector<VkImage> sharedImages;""")

    t = tracker.read_text()
    t = replace(t, '#include "ResourceTracker.h"', '''#include "ResourceTracker.h"
#include <atomic>
#include <cstdio>
#include <unordered_set>

static std::atomic<bool> rmeTrackDmaCommands{false};''')
    t = replace(t, "        info_VkDeviceMemory[*pMemory].rmeMapping = std::make_shared<RmeSharedMapping>(fd, cached != 0);",
                """        info_VkDeviceMemory[*pMemory].rmeMapping = std::make_shared<RmeSharedMapping>(fd, cached != 0);
        if (!cached) rmeTrackDmaCommands.store(true, std::memory_order_relaxed);""")

    bind_functions = (
        ("vkBindBufferMemory", "VkBuffer", "buffer", "memory"),
        ("vkBindBufferMemory2", "VkBuffer", "pBindInfos[i].buffer", "pBindInfos[i].memory"),
        ("vkBindBufferMemory2KHR", "VkBuffer", "pBindInfos[i].buffer", "pBindInfos[i].memory"),
        ("vkBindImageMemory", "VkImage", "image", "memory"),
        ("vkBindImageMemory2", "VkImage", "pBindInfos[i].image", "pBindInfos[i].memory"),
        ("vkBindImageMemory2KHR", "VkImage", "pBindInfos[i].image", "pBindInfos[i].memory"),
    )
    e = encoder.read_text()
    for name, resource, handle, memory in bind_functions:
        # Record the final encoder result so Android and KHR aliases take the same path.
        method = f"rememberShared{resource.removeprefix('Vk')}Binding"
        if name.endswith(("2", "2KHR")):
            call = f"        for (uint32_t i = 0; i < bindInfoCount; ++i) sResourceTracker->{method}({handle}, {memory});"
        else:
            call = f"        sResourceTracker->{method}({handle}, {memory});"
        result = f"{name}_VkResult_return"
        e = replace(e, f"    return {result};", f"""    if ({result} == VK_SUCCESS) {{
{call}
    }}
    return {result};""")

    for resource in ("Buffer", "Image"):
        h = replace(h, "    bool usingDirectMapping() const;", f"""    bool usingDirectMapping() const;
    void rememberShared{resource}Binding(Vk{resource} resource, VkDeviceMemory memory);""")
        t += f"""
namespace gfxstream {{
namespace vk {{
void ResourceTracker::rememberShared{resource}Binding(Vk{resource} resource, VkDeviceMemory memory) {{
    AutoLock<RecursiveLock> lock(mLock);
    auto it = info_Vk{resource}.find(resource);
    if (it != info_Vk{resource}.end()) it->second.currentBacking = memory;
}}
}}  // namespace vk
}}  // namespace gfxstream
"""

    methods = r'''
void ResourceTracker::beginSharedCommands(VkCommandBuffer command) {
    if (!rmeTrackDmaCommands.load(std::memory_order_relaxed)) return;
    AutoLock<RecursiveLock> lock(mLock);
    auto it = info_VkCommandBuffer.find(command);
    if (it == info_VkCommandBuffer.end()) return;
    it->second.sharedCommandsKnown = true;
    it->second.sharedBuffers.clear();
    it->second.sharedImages.clear();
}

void ResourceTracker::recordSharedCommand(VkCommandBuffer command,
                                         std::initializer_list<VkBuffer> buffers,
                                         std::initializer_list<VkImage> images) {
    if (!rmeTrackDmaCommands.load(std::memory_order_relaxed)) return;
    AutoLock<RecursiveLock> lock(mLock);
    auto it = info_VkCommandBuffer.find(command);
    if (it == info_VkCommandBuffer.end() || !it->second.sharedCommandsKnown) return;
    it->second.sharedBuffers.insert(it->second.sharedBuffers.end(), buffers);
    it->second.sharedImages.insert(it->second.sharedImages.end(), images);
}

void ResourceTracker::invalidateSharedCommands(VkCommandBuffer command) {
    if (!rmeTrackDmaCommands.load(std::memory_order_relaxed)) return;
    AutoLock<RecursiveLock> lock(mLock);
    auto it = info_VkCommandBuffer.find(command);
    if (it != info_VkCommandBuffer.end()) it->second.sharedCommandsKnown = false;
}

void ResourceTracker::recordSharedBarrier(VkCommandBuffer command, uint32_t bufferCount,
                                         const VkBufferMemoryBarrier* buffers, uint32_t imageCount,
                                         const VkImageMemoryBarrier* images) {
    if (!rmeTrackDmaCommands.load(std::memory_order_relaxed)) return;
    for (uint32_t i = 0; i < bufferCount; ++i) {
        const auto& barrier = buffers[i];
        if (barrier.pNext || barrier.srcQueueFamilyIndex != barrier.dstQueueFamilyIndex) {
            invalidateSharedCommands(command);
            return;
        }
        recordSharedCommand(command, {barrier.buffer}, {});
    }
    for (uint32_t i = 0; i < imageCount; ++i) {
        const auto& barrier = images[i];
        if (barrier.pNext || barrier.srcQueueFamilyIndex != barrier.dstQueueFamilyIndex) {
            invalidateSharedCommands(command);
            return;
        }
        recordSharedCommand(command, {}, {barrier.image});
    }
}

bool ResourceTracker::syncSharedSubmission(VkQueue queue, uint32_t count, const VkSubmitInfo* submits) {
    AutoLock<RecursiveLock> lock(mLock);
    const auto q = info_VkQueue.find(queue);
    if (q == info_VkQueue.end()) return false;
    const VkDevice device = q->second.device;
    if (!rmeTrackDmaCommands.load(std::memory_order_relaxed)) return syncSharedMemory(device, false);

    std::unordered_set<VkDeviceMemory> memory;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& submit = submits[i];
        if (submit.pNext || submit.waitSemaphoreCount || submit.signalSemaphoreCount) {
            return syncSharedMemory(device, false);
        }
        for (uint32_t j = 0; j < submit.commandBufferCount; ++j) {
            const auto command = info_VkCommandBuffer.find(submit.pCommandBuffers[j]);
            if (command == info_VkCommandBuffer.end() || !command->second.sharedCommandsKnown) {
                return syncSharedMemory(device, false);
            }
            for (VkBuffer buffer : command->second.sharedBuffers) {
                const auto it = info_VkBuffer.find(buffer);
                if (it == info_VkBuffer.end() || !it->second.currentBacking ||
                    (it->second.createInfo.flags & VK_BUFFER_CREATE_SPARSE_BINDING_BIT)) {
                    return syncSharedMemory(device, false);
                }
                memory.insert(it->second.currentBacking);
            }
            for (VkImage image : command->second.sharedImages) {
                const auto it = info_VkImage.find(image);
                if (it == info_VkImage.end() || !it->second.currentBacking ||
                    (it->second.createInfo.flags & (VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_DISJOINT_BIT))) {
                    return syncSharedMemory(device, false);
                }
                memory.insert(it->second.currentBacking);
            }
        }
    }

    for (VkDeviceMemory allocation : memory) {
        const auto info = info_VkDeviceMemory.find(allocation);
        if (info == info_VkDeviceMemory.end()) return syncSharedMemory(device, false);
        if (info->second.rmeMapping && !info->second.rmeMapping->sync(false)) return false;
    }
    return true;
}
'''
    if trace:
        methods = methods.replace("    for (VkDeviceMemory allocation : memory) {", r'''    static std::atomic<unsigned> samples{0};
    const unsigned sample = ++samples;
    if (sample <= 64 || sample % 256 == 0) {
        unsigned active = 0, touched = 0;
        for (const auto& [allocation, info] : info_VkDeviceMemory) {
            const auto& mapping = info.rmeMapping;
            if (info.device == device && mapping && !mapping->cached && mapping->base != MAP_FAILED) {
                ++active;
                if (memory.count(allocation)) ++touched;
            }
        }
        std::fprintf(stderr, "RME_VK_SCOPED_SUBMIT sample=%u active=%u touched=%u pid=%d\n",
                     sample, active, touched, getpid());
    }
    for (VkDeviceMemory allocation : memory) {''')
    anchor = "bool ResourceTracker::syncSharedMemory(VkDevice device, bool cpuAccess) {"
    t = replace(t, anchor, methods + "\n" + anchor)

    def inject(text, name, body):
        start = text.index(f" VkEncoder::{name}(")
        at = text.index("{\n", start) + 2
        return text[:at] + body + text[at:]

    e = replace(e, "    return vkBeginCommandBuffer_VkResult_return;", """    if (vkBeginCommandBuffer_VkResult_return == VK_SUCCESS) {
        sResourceTracker->beginSharedCommands(commandBuffer);
    } else {
        sResourceTracker->invalidateSharedCommands(commandBuffer);
    }
    return vkBeginCommandBuffer_VkResult_return;""")
    anchor = "    if (!sResourceTracker->syncSharedQueueMemory(queue, false)) return VK_ERROR_MEMORY_MAP_FAILED;"
    start = e.index("VkResult VkEncoder::vkQueueSubmit(")
    end = e.index("VkResult VkEncoder::vkQueueWaitIdle(", start)
    submit = replace(e[start:end], anchor, """    if (!sResourceTracker->syncSharedSubmission(queue, submitCount, pSubmits)) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }""")
    e = e[:start] + submit + e[end:]

    # Whitelist direct transfers and inert debug markers. Draws, dispatches,
    # secondary command buffers and unfamiliar commands use full synchronization.
    transfers = {
        "vkCmdCopyBuffer": ("srcBuffer, dstBuffer", ""),
        "vkCmdCopyBufferToImage": ("srcBuffer", "dstImage"),
        "vkCmdCopyImageToBuffer": ("dstBuffer", "srcImage"),
        "vkCmdCopyImage": ("", "srcImage, dstImage"),
        "vkCmdBlitImage": ("", "srcImage, dstImage"),
        "vkCmdResolveImage": ("", "srcImage, dstImage"),
        "vkCmdFillBuffer": ("dstBuffer", ""),
        "vkCmdUpdateBuffer": ("dstBuffer", ""),
        "vkCmdClearColorImage": ("", "image"),
        "vkCmdClearDepthStencilImage": ("", "image"),
    }
    commands = re.findall(r"^\w+ VkEncoder::(vkCmd\w+)\(\s*VkCommandBuffer commandBuffer\b", e, re.M)
    known = re.findall(r"^\w+ VkEncoder::(vkCmd\w+)\(", e, re.M)
    if commands != known:
        raise ValueError("unexpected command buffer signature for scoped DMA synchronization")
    if not transfers.keys() <= set(commands) or "vkCmdPipelineBarrier" not in commands:
        raise ValueError("missing command buffer encoders for scoped DMA synchronization")
    for name in commands:
        if name in transfers:
            buffers, images = transfers[name]
            body = f"    sResourceTracker->recordSharedCommand(commandBuffer, {{{buffers}}}, {{{images}}});\n"
        elif name == "vkCmdPipelineBarrier":
            body = """    sResourceTracker->recordSharedBarrier(commandBuffer, bufferMemoryBarrierCount, pBufferMemoryBarriers,
                                         imageMemoryBarrierCount, pImageMemoryBarriers);
"""
        elif "DebugUtilsLabel" in name or "DebugMarker" in name:
            continue
        else:
            body = "    sResourceTracker->invalidateSharedCommands(commandBuffer);\n"
        e = inject(e, name, body)

    header.write_text(h)
    tracker.write_text(t)
    encoder.write_text(e)


if __name__ == "__main__":
    apply(Path(sys.argv[1]), trace="--trace" in sys.argv[2:])
