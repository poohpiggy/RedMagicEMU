#pragma once

#include <cstdint>

namespace rme {
bool sharedMemoryEnabled();
bool sharedMemoryUsesHardwareBuffer();
uint64_t currentSharedMemoryOwner();
// The owner is the live GLProcessPipe PUID. Vulkan memory identifiers are
// meaningful only inside that owner's renderer session.
bool publishSharedMemory(uint64_t owner, uint64_t memory, int fd);
bool publishCachedSharedMemory(uint64_t owner, uint64_t memory, void* backing, uint64_t size);
void revokeSharedMemory(uint64_t owner, uint64_t memory);
int duplicateSharedMemory(uint64_t owner, uint64_t memory, bool* cached);
bool copyCachedSharedMemory(uint64_t owner, void* backing, uint64_t offset, uint64_t size, bool upload);
bool sendSharedMemory(int socket, int fd, bool cached);
}
