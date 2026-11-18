// Exercise the extracted d7a method with deterministic in-process substitutes.
// No real fence FD, socket, device, or Vulkan driver is used by the substitutes.
#include <stdint.h>
#include <stdio.h>

using VkFence = uint64_t;
using VkDevice = uint64_t;
using VkResult = int32_t;
static constexpr VkResult VK_SUCCESS = 0;
static constexpr VkResult VK_NOT_READY = 1;
static constexpr VkResult VK_ERROR_OUT_OF_HOST_MEMORY = -1;
static constexpr VkResult VK_ERROR_DEVICE_LOST = -4;
static constexpr VkResult VK_ERROR_UNKNOWN = -13;
static constexpr VkFence VK_NULL_HANDLE = 0;
// Value from the fixed Vulkan header; 0x10 is OPAQUE_WIN32_KMT, not SYNC_FD.
static constexpr uint32_t VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT = 0x8;
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
static constexpr int GOLDFISH_SYNC_VULKAN_SEMAPHORE_SYNC = 1;
#endif
static constexpr int kOutputSentinel = 197;
static constexpr int kOwnedSentinel = 198;
static constexpr int kFakeFd = 42;

struct VkFenceGetFdInfoKHR { VkFence fence; uint32_t handleType; };
struct FenceInfo { bool external; struct { uint32_t handleTypes; } exportFenceCreateInfo; int syncFd; };
struct FenceEntry { FenceInfo second; };
struct FakeFences {
    FenceEntry entry{};
    bool missing = false;
    FenceEntry* find(VkFence) { return missing ? nullptr : &entry; }
    FenceEntry* end() { return nullptr; }
};
struct FeatureInfo { bool hasVirtioGpuNativeSync = false; };
struct RecursiveLock {};
static int lock_count, unlock_count;
template <class T> struct AutoLock {
    explicit AutoLock(T&) { ++lock_count; }
    ~AutoLock() { ++unlock_count; }
};
struct VkEncoder {
    VkResult status = VK_SUCCESS;
    int status_calls = 0;
    VkResult vkGetFenceStatus(VkDevice, VkFence, bool) { ++status_calls; return status; }
};
static int queue_calls;
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
static int queue_result;
static int goldfish_sync_queue_work(int, uint64_t, int, int* fd) {
    ++queue_calls;
    *fd = queue_result == 0 ? kFakeFd : -1;
    return queue_result;
}
#endif
static uint64_t get_host_u64_VkFence(VkFence fence) { return fence; }
#define mesa_logi(...) ((void)0)

struct ResourceTracker {
    RecursiveLock mLock;
    FakeFences info_VkFence;
    FeatureInfo feature;
    FeatureInfo* mFeatureInfo = &feature;
    int mSyncDeviceFd = 777; // an integer passed only to the substitute above
    VkResult create_result = VK_SUCCESS;
    int create_calls = 0;
    VkResult createFence(VkDevice, uint64_t, int64_t& handle) {
        ++create_calls;
        if (create_result == VK_SUCCESS) handle = kFakeFd;
        return create_result;
    }
    VkResult on_vkGetFenceFdKHR(void*, VkResult, VkDevice, const VkFenceGetFdInfoKHR*, int*);
};

#include "actual-function.inc"

static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { ++failures; fprintf(stderr, "FAIL %s\n", what); }
}
static ResourceTracker tracker() {
    ResourceTracker t;
    t.info_VkFence.entry.second = {true, {VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT}, kOwnedSentinel};
    return t;
}
static void run_case(const char* name, VkResult fence_status, bool native, VkResult create_result,
                     bool missing_fence, bool bad_type, bool missing_info, bool external,
                     bool no_export_flag, VkResult expected_result, int expected_fd,
                     int expected_owned, int expected_status_calls, int expected_create_calls,
                     int expected_queue_calls) {
    ResourceTracker t = tracker();
    // Rebind after return-by-value: the feature pointer must name this object.
    t.mFeatureInfo = &t.feature;
    t.feature.hasVirtioGpuNativeSync = native;
    t.create_result = create_result;
    t.info_VkFence.missing = missing_info;
    t.info_VkFence.entry.second.external = external;
    if (no_export_flag) t.info_VkFence.entry.second.exportFenceCreateInfo.handleTypes = 0;
    VkEncoder enc;
    enc.status = fence_status;
    VkFenceGetFdInfoKHR args{missing_fence ? 0u : 123u,
                             bad_type ? 0u : VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT};
    int fd = kOutputSentinel;
    queue_calls = lock_count = unlock_count = 0;
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
    queue_result = 0;
#endif
    VkResult result = t.on_vkGetFenceFdKHR(&enc, VK_SUCCESS, 1, &args, &fd);
    check(result == expected_result, name);
    check(fd == expected_fd, name);
    check(t.info_VkFence.entry.second.syncFd == expected_owned, name);
    check(enc.status_calls == expected_status_calls, name);
    check(t.create_calls == expected_create_calls, name);
    check(queue_calls == expected_queue_calls, name);
    check(lock_count == unlock_count, name);
}

int main() {
    (void)VK_ERROR_UNKNOWN;
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
    const VkResult no_native_result = VK_SUCCESS;
    const int no_native_fd = kFakeFd, no_native_owned = -1, no_native_queue = 1;
#else
    const VkResult no_native_result = VK_ERROR_UNKNOWN;
    const int no_native_fd = kOutputSentinel, no_native_owned = kOwnedSentinel, no_native_queue = 0;
#endif
    const VkResult statuses[] = {VK_SUCCESS, VK_NOT_READY};
    for (VkResult status : statuses) {
        run_case("no backend", status, false, VK_SUCCESS, false, false, false, true, false,
                 no_native_result, no_native_fd, no_native_owned, 1, 0, no_native_queue);
        run_case("native success", status, true, VK_SUCCESS, false, false, false, true, false,
                 VK_SUCCESS, kFakeFd, -1, 1, 1, 0);
    }
    run_case("native failure", VK_SUCCESS, true, VK_ERROR_OUT_OF_HOST_MEMORY,
             false, false, false, true, false, VK_ERROR_OUT_OF_HOST_MEMORY,
             kOutputSentinel, kOwnedSentinel, 1, 1, 0);
    run_case("device lost", VK_ERROR_DEVICE_LOST, false, VK_SUCCESS,
             false, false, false, true, false, VK_ERROR_DEVICE_LOST, -1, kOwnedSentinel, 1, 0, 0);
    run_case("missing fence", VK_SUCCESS, false, VK_SUCCESS,
             true, false, false, true, false, VK_ERROR_OUT_OF_HOST_MEMORY,
             kOutputSentinel, kOwnedSentinel, 0, 0, 0);
    run_case("bad handle type", VK_SUCCESS, false, VK_SUCCESS,
             false, true, false, true, false, VK_ERROR_OUT_OF_HOST_MEMORY,
             kOutputSentinel, kOwnedSentinel, 0, 0, 0);
    run_case("missing record", VK_SUCCESS, false, VK_SUCCESS,
             false, false, true, true, false, VK_ERROR_OUT_OF_HOST_MEMORY,
             kOutputSentinel, kOwnedSentinel, 1, 0, 0);
    run_case("not external", VK_SUCCESS, false, VK_SUCCESS,
             false, false, false, false, false, VK_ERROR_OUT_OF_HOST_MEMORY,
             kOutputSentinel, kOwnedSentinel, 1, 0, 0);
    run_case("no export flag", VK_SUCCESS, false, VK_SUCCESS,
             false, false, false, true, true, VK_ERROR_OUT_OF_HOST_MEMORY,
             kOutputSentinel, kOwnedSentinel, 1, 0, 0);
#if GFXSTREAM_ENABLE_GUEST_GOLDFISH
    {
        ResourceTracker t = tracker();
        t.mFeatureInfo = &t.feature;
        VkEncoder enc;
        VkFenceGetFdInfoKHR args{123, VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT};
        int fd = kOutputSentinel;
        queue_result = -1;
        queue_calls = lock_count = unlock_count = 0;
        VkResult result = t.on_vkGetFenceFdKHR(&enc, VK_SUCCESS, 1, &args, &fd);
        check(result == VK_ERROR_UNKNOWN, "goldfish queue failure result");
        check(fd == kOutputSentinel, "goldfish queue failure output");
        check(t.info_VkFence.entry.second.syncFd == kOwnedSentinel, "goldfish queue failure ownership");
        check(queue_calls == 1 && lock_count == unlock_count, "goldfish queue failure cleanup");
    }
#endif
    if (failures) return 1;
    printf("PASS macro=%d real function with fake handles (no real fence fd)\n", GFXSTREAM_ENABLE_GUEST_GOLDFISH);
    return 0;
}
