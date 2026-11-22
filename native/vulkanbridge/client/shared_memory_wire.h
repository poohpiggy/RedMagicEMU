#pragma once

/* Mapping modes returned by rme_vulkan_memory_fd. */
enum RmeSharedMemoryMode {
    RME_MEMORY_DMA_BUF,
    RME_MEMORY_CACHED,
    RME_MEMORY_COHERENT,
};
