#include "shared_memory.h"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/memfd.h>
#include <map>
#include <mutex>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#ifdef RME_VULKAN_COHERENT_AHB_TEST
#include <android/log.h>
#endif

namespace rme {
namespace {
struct Allocation {
    int fd;
    void* backing = nullptr;
    void* cache = nullptr;
    uint64_t size = 0;
    RmeSharedMemoryMode mode = RME_MEMORY_DMA_BUF;
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

int sharedBlobDataFd(const void* address, uint64_t size, const int* fds, int count) {
    if (!address || !size || !fds || count <= 0) return -1;
    FILE* maps = std::fopen("/proc/self/maps", "re");
    if (!maps) return -1;
    const uintptr_t base = reinterpret_cast<uintptr_t>(address);
    char* line = nullptr;
    size_t capacity = 0;
    int dataFd = -1;
    while (getline(&line, &capacity, maps) >= 0) {
        char* next;
        const auto start = std::strtoull(line, &next, 16);
        if (start > base) break;
        if (start != base || *next != '-') continue;
        uintptr_t end;
        uint64_t offset, inode;
        unsigned devMajor, devMinor;
        char permissions[5];
        if (std::sscanf(next + 1, "%" SCNxPTR " %4s %" SCNx64 " %x:%x %" SCNu64,
                        &end, permissions, &offset, &devMajor, &devMinor, &inode) != 6 ||
            end <= base || end - base < size || offset || !inode ||
            permissions[0] != 'r' || permissions[3] != 's') break;
        for (int i = 0; i < count; ++i) {
            const int fd = fds[i];
            struct stat backing{};
            if (fstat(fd, &backing) != 0 || backing.st_ino != inode ||
                major(backing.st_dev) != devMajor || minor(backing.st_dev) != devMinor) continue;
            const int flags = fcntl(fd, F_GETFL);
            if (flags < 0 || (flags & O_ACCMODE) != O_RDWR) continue;
            const auto bytes = lseek(fd, 0, SEEK_END);
            if (bytes >= 0 && static_cast<uint64_t>(bytes) >= size && lseek(fd, 0, SEEK_SET) == 0) {
                dataFd = fd;
                break;
            }
        }
        break;
    }
    std::free(line);
    std::fclose(maps);
    return dataFd;
}

bool publishSharedMemory(uint64_t owner, uint64_t memory, int fd, bool coherent) {
    if (!owner || !memory) return false;
    const int copy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (copy < 0) return false;
    std::lock_guard lock(memoryMutex);
    Allocation allocation{copy};
    if (coherent) allocation.mode = RME_MEMORY_COHERENT;
    if (allocations.emplace(std::make_pair(owner, memory), allocation).second) {
#ifdef RME_VULKAN_COHERENT_AHB_TEST
        static unsigned coherentCount = 0;
        static unsigned fallbackCount = 0;
        unsigned count = coherent ? ++coherentCount : ++fallbackCount;
        if (count == 1 || count % 1024 == 0)
            __android_log_print(ANDROID_LOG_INFO, "RedMagicEMU-Vulkan",
                                "Shared BLOB %s count=%u", coherent ? "coherent" : "DMA-BUF fallback", count);
#endif
        return true;
    }
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
    if (allocations.emplace(std::make_pair(owner, memory),
                            Allocation{fd, backing, cache, size, RME_MEMORY_CACHED}).second) {
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

int duplicateSharedMemory(uint64_t owner, uint64_t memory, RmeSharedMemoryMode* mode) {
    std::lock_guard lock(memoryMutex);
    const auto entry = allocations.find({owner, memory});
    if (entry == allocations.end()) return -1;
    *mode = entry->second.mode;
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

bool sendSharedMemory(int socket, int fd, RmeSharedMemoryMode mode) {
    char status = fd < 0 ? 'N' : mode == RME_MEMORY_CACHED ? 'C' :
                                mode == RME_MEMORY_COHERENT ? 'H' : 'M';
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
