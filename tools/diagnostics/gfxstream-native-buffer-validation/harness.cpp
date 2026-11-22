// CPU-only extracted gfxstream native-buffer preflights; no actual buffer or GPU.
#include <assert.h>
#include <stdint.h>
#define VK_USE_PLATFORM_ANDROID_KHR 1
using VkResult = int;
enum { VK_SUCCESS = 0, VK_ERROR_DEVICE_LOST = -4, VK_ERROR_INVALID_EXTERNAL_HANDLE = -1000072003 };
using VkDevice = void*;
using VkImage = void*;
struct native_handle_t { int unused; };
struct VkAllocationCallbacks {};
struct VkNativeBufferANDROID { const native_handle_t *handle; };
struct VkImageCreateInfo { const VkNativeBufferANDROID *native; };
template <typename T> const T *vk_find_struct(const VkImageCreateInfo *info) {
    return reinterpret_cast<const T *>(info->native);
}
struct VkEncoder {};
struct AHardwareBuffer {};
struct VkAndroidHardwareBufferFormatPropertiesANDROID {};
struct VkAndroidHardwareBufferPropertiesANDROID {};
template <typename T> T *vk_find_struct(VkAndroidHardwareBufferPropertiesANDROID *) {
    return nullptr;
}
struct Gralloc {
    uint32_t value = 0;
    uint32_t format_queries = 0;
    uint32_t getHostHandle(const native_handle_t *) { return value; }
    uint32_t getHostHandle(const AHardwareBuffer *) { return value; }
    int getFormat(const AHardwareBuffer *) {
        ++format_queries;
        assert(value && "format requires a supported host handle");
        return 1;
    }
};
namespace gfxstream { using Gralloc = ::Gralloc; }
struct Connection { Gralloc *helper = nullptr; Gralloc *grallocHelper() { return helper; } };
static Connection *active_connection;
static Connection *get_connection() { return active_connection; }
struct ResourceTracker {
    struct Callbacks { Connection *(*hostConnectionGetFunc)(); };
    static Callbacks threadingCallbacks;
    VkResult on_vkCreateImage(void*, VkResult, VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage*);
};
ResourceTracker::Callbacks ResourceTracker::threadingCallbacks = {get_connection};
@CHECKPOINT@
@PROPERTIES_CHECKPOINT@
int main() {
    ResourceTracker tracker;
    VkImageCreateInfo empty = {nullptr};
    assert(tracker.on_vkCreateImage(nullptr, 0, nullptr, &empty, nullptr, nullptr) == VK_SUCCESS);
    native_handle_t handle{};
    VkNativeBufferANDROID native{&handle};
    VkImageCreateInfo image{&native};
    assert(tracker.on_vkCreateImage(nullptr, 0, nullptr, &image, nullptr, nullptr) == VK_ERROR_DEVICE_LOST);
    Connection connection;
    active_connection = &connection;
    assert(tracker.on_vkCreateImage(nullptr, 0, nullptr, &image, nullptr, nullptr) == VK_ERROR_DEVICE_LOST);
    Gralloc gralloc;
    connection.helper = &gralloc;
    assert(tracker.on_vkCreateImage(nullptr, 0, nullptr, &image, nullptr, nullptr) == VK_ERROR_INVALID_EXTERNAL_HANDLE);
    gralloc.value = 23;
    assert(tracker.on_vkCreateImage(nullptr, 0, nullptr, &image, nullptr, nullptr) == VK_SUCCESS);
    native.handle = nullptr;
    assert(tracker.on_vkCreateImage(nullptr, 0, nullptr, &image, nullptr, nullptr) == VK_ERROR_INVALID_EXTERNAL_HANDLE);

    AHardwareBuffer buffer;
    VkAndroidHardwareBufferPropertiesANDROID properties;
    gralloc.value = 0;
    assert(getAndroidHardwareBufferPropertiesANDROID(&gralloc, &buffer, &properties) ==
           VK_ERROR_INVALID_EXTERNAL_HANDLE);
    assert(gralloc.format_queries == 0);
    gralloc.value = 23;
    assert(getAndroidHardwareBufferPropertiesANDROID(&gralloc, &buffer, &properties) == VK_SUCCESS);
    assert(gralloc.format_queries == 1);
}
