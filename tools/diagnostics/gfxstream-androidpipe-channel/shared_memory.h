#pragma once

#include <cstdint>
#include "../../../native/vulkanbridge/client/shared_memory_wire.h"

namespace rme {
bool sharedMemoryEnabled();
bool sharedMemoryUsesHardwareBuffer();
uint64_t currentSharedMemoryOwner();
// The locked BLOB address must map the descriptor from offset zero.
int sharedBlobDataFd(const void* address, uint64_t size, const int* fds, int count);
// The owner is the live GLProcessPipe PUID. Vulkan memory identifiers are
// meaningful only inside that owner's renderer session.
bool publishSharedMemory(uint64_t owner, uint64_t memory, int fd, bool coherent = false);
bool publishCachedSharedMemory(uint64_t owner, uint64_t memory, void* backing, uint64_t size);
void revokeSharedMemory(uint64_t owner, uint64_t memory);
int duplicateSharedMemory(uint64_t owner, uint64_t memory, RmeSharedMemoryMode* mode);
bool copyCachedSharedMemory(uint64_t owner, void* backing, uint64_t offset, uint64_t size, bool upload);
bool sendSharedMemory(int socket, int fd, RmeSharedMemoryMode mode);
}
