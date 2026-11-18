#!/usr/bin/env python3
"""Adapt the paired Vulkan decoder for legacy layer names and cached mappings."""

from pathlib import Path
import sys

source, output = map(Path, sys.argv[1:])
text = source.read_text()
for opcode in ("vkEnumerateInstanceExtensionProperties", "vkEnumerateDeviceExtensionProperties"):
    start = text.index("case OP_" + opcode + ": {")
    end = text.index("case OP_", start + 1)
    block = text[start:end]
    string_read = """      } else {
        vkReadStream->loadStringInPlaceWithStreamPtr((char **)&pLayerName,
                                                     readStreamPtrPtr);
      }"""
    if block.count(string_read) != 1:
        raise ValueError("unexpected layer string decoder in " + opcode)
    read_and_normalize = string_read.rsplit("\n", 1)[0] + """
        if (pLayerName && !*pLayerName) pLayerName = nullptr;
      }"""
    block = block.replace(string_read, read_and_normalize)
    text = text[:start] + block + text[end:]


def replace(old, new):
    global text
    if text.count(old) != 1:
        raise ValueError("unexpected decoder source near " + old[:65])
    text = text.replace(old, new, 1)


replace('#include "VkDecoder.h"', '''#include "VkDecoder.h"
#include "shared_memory.h"''')
replace("namespace gfxstream {\nnamespace vk {", """namespace gfxstream {
namespace vk {

static bool syncCachedMappings(VkDecoderGlobalState* state, uint32_t count,
                               const VkMappedMemoryRange* ranges, bool upload) {
    const auto owner = rme::currentSharedMemoryOwner();
    if (!owner) return true;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& range = ranges[i];
        auto* backing = state->getMappedHostPointer(range.memory);
        if (!rme::copyCachedSharedMemory(owner, backing, range.offset, range.size, upload))
            return false;
    }
    return true;
}
""")
replace('''      vkFlushMappedMemoryRanges_VkResult_return = vk->vkFlushMappedMemoryRanges(
          unboxed_device, memoryRangeCount, pMemoryRanges);''',
        '''      vkFlushMappedMemoryRanges_VkResult_return =
          syncCachedMappings(m_state, memoryRangeCount, pMemoryRanges, true)
              ? vk->vkFlushMappedMemoryRanges(unboxed_device, memoryRangeCount, pMemoryRanges)
              : VK_ERROR_MEMORY_MAP_FAILED;''')
replace('''          vk->vkInvalidateMappedMemoryRanges(unboxed_device, memoryRangeCount,
                                             pMemoryRanges);''',
        '''          vk->vkInvalidateMappedMemoryRanges(unboxed_device, memoryRangeCount,
                                             pMemoryRanges);
      if (vkInvalidateMappedMemoryRanges_VkResult_return == VK_SUCCESS &&
          !syncCachedMappings(m_state, memoryRangeCount, pMemoryRanges, false)) {
        vkInvalidateMappedMemoryRanges_VkResult_return = VK_ERROR_MEMORY_MAP_FAILED;
      }''')
output.write_text(text)
