#include "shared_memory.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/memfd.h>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace rme {
namespace {
struct Allocation {
    int fd;
    void* backing = nullptr;
    void* cache = nullptr;
    uint64_t size = 0;
};
std::mutex memoryMutex;
std::map<std::pair<uint64_t, uint64_t>, Allocation> allocations;
std::map<std::pair<uint64_t, void*>, uint64_t> caches;

enum class Mode { Disabled, HardwareBuffer, Cached };

Mode mode() {
    static const auto selected = [] {
        const char* value = std::getenv("RME_VULKAN_SHARED_MEMORY");
        if (value && std::strcmp(value, "1") == 0) return Mode::HardwareBuffer;
        if (value && std::strcmp(value, "cached") == 0) return Mode::Cached;
        return Mode::Disabled;
    }();
    return selected;
}
}

bool sharedMemoryEnabled() {
    return mode() != Mode::Disabled;
}

bool sharedMemoryUsesHardwareBuffer() {
    return mode() == Mode::HardwareBuffer;
}

bool publishSharedMemory(uint64_t owner, uint64_t memory, int fd) {
    if (!owner || !memory) return false;
    const int copy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (copy < 0) return false;
    std::lock_guard lock(memoryMutex);
    if (allocations.emplace(std::make_pair(owner, memory), Allocation{copy}).second) return true;
    close(copy);
    return false;
}

bool publishCachedSharedMemory(uint64_t owner, uint64_t memory, void* backing, uint64_t size) {
    if (!owner || !memory || !backing || !size || size > SIZE_MAX || size > INT64_MAX) return false;
    const int fd = syscall(SYS_memfd_create, "rme-vulkan-cache", MFD_CLOEXEC);
    if (fd < 0) return false;
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        close(fd);
        return false;
    }
    void* cache = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cache == MAP_FAILED) {
        close(fd);
        return false;
    }
    std::lock_guard lock(memoryMutex);
    if (allocations.emplace(std::make_pair(owner, memory), Allocation{fd, backing, cache, size}).second) {
        caches.emplace(std::make_pair(owner, backing), memory);
        return true;
    }
    munmap(cache, size);
    close(fd);
    return false;
}

void revokeSharedMemory(uint64_t owner, uint64_t memory) {
    std::lock_guard lock(memoryMutex);
    const auto entry = allocations.find({owner, memory});
    if (entry == allocations.end()) return;
    const auto& allocation = entry->second;
    if (allocation.backing) {
        caches.erase({owner, allocation.backing});
        munmap(allocation.cache, allocation.size);
    }
    close(allocation.fd);
    allocations.erase(entry);
}

int duplicateSharedMemory(uint64_t owner, uint64_t memory, bool* cached) {
    std::lock_guard lock(memoryMutex);
    const auto entry = allocations.find({owner, memory});
    if (entry == allocations.end()) return -1;
    *cached = entry->second.backing != nullptr;
    return fcntl(entry->second.fd, F_DUPFD_CLOEXEC, 0);
}

bool copyCachedSharedMemory(uint64_t owner, void* backing, uint64_t offset, uint64_t size, bool upload) {
    std::lock_guard lock(memoryMutex);
    const auto cache = caches.find({owner, backing});
    if (cache == caches.end()) return true;
    const auto& allocation = allocations.at({owner, cache->second});
    if (offset > allocation.size || size > allocation.size - offset) return false;
    auto* native = static_cast<char*>(allocation.backing) + offset;
    auto* guest = static_cast<char*>(allocation.cache) + offset;
    if (upload) std::memcpy(native, guest, size);
    else std::memcpy(guest, native, size);
    return true;
}

bool sendSharedMemory(int socket, int fd, bool cached) {
    char status = fd < 0 ? 'N' : (cached ? 'C' : 'M');
    iovec data{&status, 1};
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
    msghdr reply{};
    reply.msg_iov = &data;
    reply.msg_iovlen = 1;
    if (fd >= 0) {
        reply.msg_control = control;
        reply.msg_controllen = sizeof(control);
        auto* header = CMSG_FIRSTHDR(&reply);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(header), &fd, sizeof(fd));
    }
    ssize_t count;
    do {
        count = sendmsg(socket, &reply, MSG_NOSIGNAL);
    } while (count < 0 && errno == EINTR);
    return count == 1;
}
}
