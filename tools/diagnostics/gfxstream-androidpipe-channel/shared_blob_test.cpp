#include "shared_memory.h"

#include <cassert>
#include <cstdio>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using VkDeviceSize = uint64_t;

constexpr uint32_t AHARDWAREBUFFER_FORMAT_BLOB = 33;
constexpr uint64_t AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER = 1 << 24;
constexpr uint64_t AHARDWAREBUFFER_USAGE_CPU_READ_RARELY = 2;

struct AHardwareBuffer_Desc {
    uint32_t width, height, layers, format;
    uint64_t usage;
};

struct native_handle_t {
    int numFds, numInts, data[3];
};

struct AHardwareBuffer {
    native_handle_t handle;
    void* data;
    AHardwareBuffer_Desc desc;
    bool locked = false;
};

extern "C" const native_handle_t* AHardwareBuffer_getNativeHandle(const AHardwareBuffer* buffer) {
    return &buffer->handle;
}

void AHardwareBuffer_describe(const AHardwareBuffer* buffer, AHardwareBuffer_Desc* desc) {
    *desc = buffer->desc;
}

int AHardwareBuffer_lock(AHardwareBuffer* buffer, uint64_t usage, int fence, const void*, void** data) {
    assert(!buffer->locked && usage == AHARDWAREBUFFER_USAGE_CPU_READ_RARELY && fence == -1);
    buffer->locked = true;
    *data = buffer->data;
    return 0;
}

int AHardwareBuffer_unlock(AHardwareBuffer* buffer, int* fence) {
    assert(buffer->locked && !fence);
    buffer->locked = false;
    return 0;
}

constexpr int ANDROID_LOG_WARN = 5;
int __android_log_print(int, const char*, const char*, ...) {
    return 0;
}

#include "shared_blob_publish.inc"

int makeBacking(off_t size) {
    const int fd = memfd_create("shared-blob-test", MFD_CLOEXEC);
    assert(fd >= 0 && ftruncate(fd, size) == 0);
    return fd;
}

void expectDataFd(AHardwareBuffer* buffer, size_t size, int data, const void* native = nullptr,
                  RmeSharedMemoryMode expectedMode = RME_MEMORY_DMA_BUF) {
    constexpr uint64_t owner = 1, memory = 2;
    assert(publishSharedBlob(buffer, size, owner, memory, native));
    assert(!buffer->locked);
    RmeSharedMemoryMode mode = RME_MEMORY_CACHED;
    const int fd = rme::duplicateSharedMemory(owner, memory, &mode);
    struct stat received{}, expected{};
    assert(fd >= 0 && mode == expectedMode &&
           fstat(fd, &received) == 0 && fstat(data, &expected) == 0);
    assert(received.st_dev == expected.st_dev && received.st_ino == expected.st_ino);
    close(fd);
    rme::revokeSharedMemory(owner, memory);
}

int main() {
    const auto page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    assert(page > 0);
    const int data = makeBacking(page * 2);
    const int metadata = makeBacking(page * 2);
    void* mapping = mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, data, 0);
    assert(mapping != MAP_FAILED);
    AHardwareBuffer buffer{{2, 26, {data, metadata}}, mapping,
                           {static_cast<uint32_t>(page), 1, 1, AHARDWAREBUFFER_FORMAT_BLOB,
                            AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER}};
    expectDataFd(&buffer, page, data);
    expectDataFd(&buffer, page, data, mapping, RME_MEMORY_COHERENT);
    expectDataFd(&buffer, page, data, static_cast<char*>(mapping) + 1);
    std::puts("PASS: original two-FD layout");
    std::fflush(stdout);

    buffer.handle = {2, 8, {metadata, data}};
    expectDataFd(&buffer, page, data);
    buffer.handle = {1, 4, {data}};
    expectDataFd(&buffer, page, data);
    buffer.handle = {2, 26, {metadata, data}};
    expectDataFd(&buffer, page, data);
    buffer.handle = {1, 8, {metadata}};
    assert(!publishSharedBlob(&buffer, page, 1, 2));
    assert(!buffer.locked);

    assert(munmap(mapping, page) == 0);
    buffer.handle = {1, 8, {data}};
    mapping = mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, data, page);
    assert(mapping != MAP_FAILED);
    buffer.data = mapping;
    assert(!publishSharedBlob(&buffer, page, 1, 2));
    assert(!buffer.locked);
    assert(munmap(mapping, page) == 0);

    mapping = mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_PRIVATE, data, 0);
    assert(mapping != MAP_FAILED);
    buffer.data = mapping;
    assert(!publishSharedBlob(&buffer, page, 1, 2));
    assert(!buffer.locked);
    assert(munmap(mapping, page) == 0);
    close(metadata);
    close(data);
    std::puts("PASS: variable FD layouts and unmappable backing rejection");
}
