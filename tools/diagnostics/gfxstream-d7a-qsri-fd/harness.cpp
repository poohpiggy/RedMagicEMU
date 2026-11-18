// Exercises extracted gfxstream methods with substitutes only. Fake FDs never
// reach OS open, dup, close, ioctl, sockets, a device, or a Vulkan driver.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

using VkResult = int32_t;
using VkImage = uint64_t;
using VkQueue = uint64_t;
using VkSemaphore = uint64_t;
static constexpr VkResult VK_SUCCESS = 0;
static constexpr VkResult VK_ERROR_OUT_OF_HOST_MEMORY = -1;
static constexpr VkResult VK_ERROR_INITIALIZATION_FAILED = -3;
static constexpr VkResult VK_ERROR_UNKNOWN = -13;
static constexpr int kSentinel = 197;
static constexpr int kPendingFd = 198;
static constexpr int kNativeFd = 42;
static constexpr int kGoldfishFd = 53;
static constexpr int kDirectFd = 64;
static constexpr int kDuplicatedFd = 142;
static constexpr int ETIME_FAKE = 62;
static int fake_errno;
#define errno fake_errno
#define ETIME ETIME_FAKE
#define mesa_logi(...) ((void)0)
#define mesa_loge(...) ((void)0)

static int queue_calls, exec_calls, wait_calls, dup_calls, close_calls;
static int closed_fd, last_dup_fd, lock_count, unlock_count;
static int goldfish_calls;
static int exec_result;

struct RecursiveLock {};
template <class T> struct AutoLock {
    explicit AutoLock(T&) { ++lock_count; }
    ~AutoLock() { ++unlock_count; }
};

struct FakeFdList {
    int values[4] = {kPendingFd, 0, 0, 0};
    unsigned count = 1;
    int* begin() { return values; }
    int* end() { return values + count; }
    int* erase(int* it) {
        unsigned pos = static_cast<unsigned>(it - values);
        for (unsigned i = pos + 1; i < count; ++i) values[i - 1] = values[i];
        --count;
        return values + pos;
    }
    void push_back(int fd) { if (count < 4) values[count++] = fd; }
};
struct ImageInfo { FakeFdList pendingQsriSyncFds; };
struct ImageEntry { ImageInfo second; };
struct FakeImages {
    ImageEntry entry{};
    bool missing = false;
    int lookups = 0;
    ImageEntry* find(VkImage) { ++lookups; return missing ? nullptr : &entry; }
    ImageEntry* end() { return nullptr; }
};
struct FeatureInfo { bool hasVirtioGpuNativeSync = false; bool hasVulkanAsyncQsri = true; };
struct FakeSyncHelper {
    int wait(int, int) { ++wait_calls; fake_errno = ETIME; return -1; }
    int dup(int fd) { ++dup_calls; last_dup_fd = fd; return kDuplicatedFd; }
    int close(int fd) { ++close_calls; closed_fd = fd; return 0; }
};
static FakeSyncHelper fake_sync;
struct FakeHostConnection { FakeSyncHelper* syncHelper() { return &fake_sync; } };
static FakeHostConnection fake_host;
static FakeHostConnection* fake_connection() { return &fake_host; }
struct FakeCallbacks { FakeHostConnection* (*hostConnectionGetFunc)(); };

static constexpr unsigned kFenceOut = 1, kRingIdx = 2, GFXSTREAM_CREATE_QSRI_EXPORT_VK = 3;
struct VirtGpuExecBuffer { void* command = nullptr; size_t command_size = 0; unsigned flags = 0; struct { int osHandle = 0; } handle; };
struct gfxstreamCreateQSRIExportVK { struct { unsigned opCode = 0; } hdr; uint32_t imageHandleLo = 0, imageHandleHi = 0; };
struct VirtGpuDevice {
    static VirtGpuDevice* getInstance() { static VirtGpuDevice d; return &d; }
    int execBuffer(VirtGpuExecBuffer& exec, void*) { ++exec_calls; exec.handle.osHandle = kNativeFd; return exec_result; }
};
static uint64_t get_host_u64_VkImage(VkImage image) { return image; }
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
static constexpr int GOLDFISH_SYNC_VULKAN_QSRI = 2;
static int goldfish_sync_queue_work(int, uint64_t, int, int* fd) {
    ++goldfish_calls;
    *fd = kGoldfishFd;
    return 0;
}
#endif

struct VkEncoder {
    int direct_calls = 0, async_calls = 0;
    VkResult direct_result = VK_SUCCESS;
    VkResult vkQueueSignalReleaseImageANDROID(VkQueue, uint32_t, const VkSemaphore*, VkImage,
                                              int* fd, bool) {
        ++direct_calls;
        if (fd) *fd = kDirectFd;
        return direct_result;
    }
    void vkQueueSignalReleaseImageANDROIDAsyncGOOGLE(VkQueue, uint32_t, const VkSemaphore*, VkImage,
                                                     bool) { ++async_calls; }
};
struct ResourceTracker {
    static FakeCallbacks threadingCallbacks;
    RecursiveLock mLock;
    FakeImages info_VkImage;
    FeatureInfo feature;
    FeatureInfo* mFeatureInfo = &feature;
    int mSyncDeviceFd = 777; // passed only to the fake Goldfish helper
    int sync_init_calls = 0;
    void ensureSyncDeviceFd() { ++sync_init_calls; }
    VkResult exportSyncFdForQSRILocked(VkImage, int*);
    VkResult on_vkQueueSignalReleaseImageANDROID(void*, VkResult, VkQueue, uint32_t,
                                                 const VkSemaphore*, VkImage, int*);
};
FakeCallbacks ResourceTracker::threadingCallbacks{fake_connection};

#include "actual-functions.inc"

static int failures;
static void check(bool ok, const char* name, const char* detail) {
    if (!ok) { ++failures; fprintf(stderr, "FAIL %s: %s\n", name, detail); }
}
static void reset() {
    queue_calls = exec_calls = wait_calls = dup_calls = close_calls = 0;
    goldfish_calls = exec_result = lock_count = unlock_count = 0;
    closed_fd = last_dup_fd = -999;
    fake_errno = 0;
}
static void direct_helper_no_backend() {
    reset();
    ResourceTracker t;
    int fd = kSentinel;
    const VkResult result = t.exportSyncFdForQSRILocked(123, &fd);
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
    check(result == VK_SUCCESS && fd == kGoldfishFd, "helper goldfish", "result/fd");
    check(goldfish_calls == 1 && t.sync_init_calls == 1, "helper goldfish", "fake backend calls");
    check(wait_calls == 1 && dup_calls == 1 && t.info_VkImage.entry.second.pendingQsriSyncFds.count == 2,
          "helper goldfish", "pending fd bookkeeping");
#else
    check(result == VK_ERROR_UNKNOWN && fd == -1, "helper missing backend", "explicit failure and fd");
    check(t.info_VkImage.lookups == 0 && t.info_VkImage.entry.second.pendingQsriSyncFds.count == 1,
          "helper missing backend", "image state unchanged");
    check(!goldfish_calls && !wait_calls && !dup_calls && !close_calls,
          "helper missing backend", "no fake sync operations");
#endif
}
static void async_no_backend(bool output_pointer) {
    reset();
    ResourceTracker t;
    VkEncoder enc;
    int fd = kSentinel;
    const VkResult result = t.on_vkQueueSignalReleaseImageANDROID(&enc, VK_SUCCESS, 1, 0, nullptr,
                                                                  123, output_pointer ? &fd : nullptr);
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
    check(result == VK_SUCCESS && (!output_pointer || fd == kGoldfishFd), "async goldfish", "result/fd");
    check(enc.async_calls == 1 && goldfish_calls == 1 && t.sync_init_calls == 1, "async goldfish", "fake calls");
    check(close_calls == (output_pointer ? 0 : 1), "async goldfish", "fake fd ownership");
    if (!output_pointer) check(closed_fd == kGoldfishFd, "async goldfish", "closed only fake fd");
#else
    check(result == VK_ERROR_UNKNOWN && (!output_pointer || fd == -1), "async missing backend", "explicit failure and fd");
    check(enc.async_calls == 0 && enc.direct_calls == 0, "async missing backend", "reject before submit");
    check(t.info_VkImage.entry.second.pendingQsriSyncFds.count == 1, "async missing backend", "image state unchanged");
    check(!goldfish_calls && !wait_calls && !dup_calls && !close_calls, "async missing backend", "no fake sync operations");
#endif
    check(lock_count == unlock_count, "async missing backend", "balanced lock");
}
static void native_case(bool output_pointer, bool fail) {
    reset();
    exec_result = fail ? -1 : 0;
    ResourceTracker t;
    t.feature.hasVirtioGpuNativeSync = true;
    VkEncoder enc;
    int fd = kSentinel;
    const VkResult result = t.on_vkQueueSignalReleaseImageANDROID(&enc, VK_SUCCESS, 1, 0, nullptr,
                                                                  123, output_pointer ? &fd : nullptr);
    check(result == (fail ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS), "native", "result");
    check(enc.async_calls == 1 && exec_calls == 1 && !goldfish_calls, "native", "backend and submit calls");
    check(fd == (output_pointer && !fail ? kNativeFd : kSentinel), "native", "output contract");
    check(close_calls == (!fail && !output_pointer ? 1 : 0), "native", "close only on successful owned fake fd");
    if (!fail && !output_pointer) check(closed_fd == kNativeFd, "native", "closed only fake fd");
    check(t.info_VkImage.entry.second.pendingQsriSyncFds.count == (fail ? 1u : 2u), "native", "pending state");
}
static void sync_direct(bool output_pointer) {
    reset();
    ResourceTracker t;
    t.feature.hasVulkanAsyncQsri = false;
    VkEncoder enc;
    int fd = kSentinel;
    const VkResult result = t.on_vkQueueSignalReleaseImageANDROID(&enc, VK_SUCCESS, 1, 0, nullptr,
                                                                  123, output_pointer ? &fd : nullptr);
    check(result == VK_SUCCESS && (!output_pointer || fd == kDirectFd), "sync direct", "result/fd");
    check(enc.direct_calls == 1 && enc.async_calls == 0 && t.info_VkImage.lookups == 0,
          "sync direct", "direct path unchanged");
    check(!goldfish_calls && !exec_calls && !wait_calls && !dup_calls && !close_calls,
          "sync direct", "no fake sync side effects");
}
static void missing_image() {
    reset();
    ResourceTracker t;
    t.info_VkImage.missing = true;
    VkEncoder enc;
    int fd = kSentinel;
    const VkResult result = t.on_vkQueueSignalReleaseImageANDROID(&enc, VK_SUCCESS, 1, 0, nullptr, 123, &fd);
    check(result == VK_ERROR_INITIALIZATION_FAILED && fd == -1 && enc.async_calls == 0,
          "missing image", "existing failure priority");
}
int main() {
    (void)VK_ERROR_UNKNOWN;
    (void)kGoldfishFd;
    direct_helper_no_backend();
    async_no_backend(true); async_no_backend(false);
    native_case(true, false); native_case(false, false);
    native_case(true, true); native_case(false, true);
    sync_direct(true); sync_direct(false);
    missing_image();
    if (failures) return 1;
    printf("PASS macro=%d actual QSRI functions with fake fence FDs\n", GFXSTREAM_ENABLE_GUEST_GOLDFISH);
    return 0;
}
