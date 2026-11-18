#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using VkDevice = uint64_t; using VkImage = uint64_t; using VkPhysicalDevice = uint64_t; using VkResult = int;
constexpr VkResult VK_SUCCESS = 0, VK_ERROR_OUT_OF_HOST_MEMORY = -1, VK_ERROR_DEVICE_LOST = -4, VK_ERROR_UNKNOWN = -13;
struct VkImageCreateInfo { int format = 0; int initialLayout = 0; };
struct VkAllocationCallbacks {};
struct VkPhysicalDeviceMemoryProperties {};
struct VkNativeBufferANDROID {};
namespace android { namespace base {
struct BumpPool {};
template<class M, class K> auto find(M& map, K key) -> typename M::mapped_type* {
    auto it = map.find(key); return it == map.end() ? nullptr : &it->second;
}
}}
#define INFO(...) ((void)0)
static VkImage nextImage = 0;
static bool nativeRequested = false;
static int boxesCreated = 0;
struct VulkanDispatch {
    VkResult createResult = VK_SUCCESS; int createCalls = 0, destroyCalls = 0;
    VkDevice destroyedDevice = 0; VkImage destroyedImage = 0; const VkAllocationCallbacks* destroyedAllocator = nullptr;
    VkResult vkCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* out) {
        ++createCalls;
        if (createResult == VK_SUCCESS) *out = nextImage;
        return createResult;
    }
    void vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* allocator) {
        ++destroyCalls; destroyedDevice = device; destroyedImage = image; destroyedAllocator = allocator;
    }
} driver;
struct AndroidNativeBufferInfo {
    VulkanDispatch* vk = nullptr; VkDevice device = 0; VkImage image = 0;
    ~AndroidNativeBufferInfo() { if (vk && image) vk->vkDestroyImage(device, image, nullptr); }
};
struct CompressedImageInfo {
    CompressedImageInfo() = default;
    explicit CompressedImageInfo(VkDevice) {}
    CompressedImageInfo(VkDevice, const VkImageCreateInfo&, void*) {}
    VkImageCreateInfo getOutputCreateInfo(const VkImageCreateInfo& info) { return info; }
    void setOutputImage(VkImage) {}
    void createCompressedMipmapImages(VulkanDispatch*, const VkImageCreateInfo&) {}
    bool isAstc() { return false; }
    void initAstcCpuDecompression(VulkanDispatch*, VkPhysicalDevice) {}
};
struct ImageInfo { VkDevice device = 0; CompressedImageInfo cmpInfo; VkImageCreateInfo imageCreateInfoShallow; int layout = 0; std::shared_ptr<AndroidNativeBufferInfo> anbInfo; };
struct DeviceInfo {
    std::unordered_set<int> imageFormats; std::unique_ptr<int> decompPipelines;
    bool useAstcCpuDecompression = false; VkPhysicalDevice physicalDevice = 0;
    bool needEmulatedDecompression(int) { return false; }
};
struct MemoryHelper { VkPhysicalDeviceMemoryProperties value; const VkPhysicalDeviceMemoryProperties& getHostMemoryProperties() { return value; } };
struct PhysicalDeviceInfo { std::unique_ptr<MemoryHelper> memoryPropertiesHelper = std::make_unique<MemoryHelper>(); };
static VkDevice unbox_VkDevice(VkDevice d) { return d; }
static VulkanDispatch* dispatch_VkDevice(VkDevice) { return &driver; }
template<class T> const T* vk_find_struct(const VkImageCreateInfo*) { static T value; return nativeRequested ? &value : nullptr; }
static VkResult prepareAndroidNativeBufferImage(VulkanDispatch* vk, VkDevice device, android::base::BumpPool&,
    const VkImageCreateInfo*, const VkNativeBufferANDROID*, const VkAllocationCallbacks*,
    const VkPhysicalDeviceMemoryProperties*, AndroidNativeBufferInfo* out) {
    out->vk = vk; out->device = device; out->image = nextImage; return VK_SUCCESS;
}
static VkImageCreateInfo vk_make_orphan_copy(const VkImageCreateInfo& info) { return info; }
static VkImage new_boxed_non_dispatchable_VkImage(VkImage image) { ++boxesCreated; return image + 1000; }
struct Handler {
    /* EXTRACTED_METHOD */
    std::recursive_mutex mLock;
    std::unordered_map<VkDevice, DeviceInfo> mDeviceInfo;
    std::unordered_map<VkDevice, VkPhysicalDevice> mDeviceToPhysicalDevice;
    std::unordered_map<VkPhysicalDevice, PhysicalDeviceInfo> mPhysdevInfo;
    std::unordered_map<VkImage, ImageInfo> mImageInfo;
    std::unordered_map<VkImage, uint64_t> mSwapchainImageOwners;
    VulkanDispatch* m_vk = &driver;
};
int main() {
    Handler h; h.mDeviceInfo.try_emplace(1); h.mDeviceInfo.try_emplace(2);
    h.mDeviceToPhysicalDevice[2] = 7; h.mPhysdevInfo.try_emplace(7);
    h.mImageInfo[42].device = 1; h.mSwapchainImageOwners[42] = 10;
    VkAllocationCallbacks allocator; VkImageCreateInfo info{}; android::base::BumpPool pool;
    nextImage = 42; VkImage image = 999;
    if (h.on_vkCreateImage(&pool, 2, &info, &allocator, &image) != VK_ERROR_UNKNOWN || image ||
        driver.destroyCalls != 1 || driver.destroyedDevice != 2 || driver.destroyedImage != 42 ||
        driver.destroyedAllocator != &allocator || h.mImageInfo.at(42).device != 1 || boxesCreated) return 1;
    nativeRequested = true; image = 999;
    if (h.on_vkCreateImage(&pool, 2, &info, nullptr, &image) != VK_ERROR_UNKNOWN || image ||
        driver.destroyCalls != 2 || driver.destroyedDevice != 2 || h.mImageInfo.at(42).device != 1 || boxesCreated) return 2;
    nativeRequested = false; nextImage = 43; image = 0;
    if (h.on_vkCreateImage(&pool, 2, &info, nullptr, &image) != VK_SUCCESS || image != 1043 ||
        driver.destroyCalls != 2 || h.mImageInfo.at(43).device != 2 || boxesCreated != 1) return 3;
    driver.createResult = VK_ERROR_DEVICE_LOST; image = 0;
    if (h.on_vkCreateImage(&pool, 2, &info, nullptr, &image) != VK_ERROR_DEVICE_LOST ||
        driver.destroyCalls != 2 || boxesCreated != 1) return 4;
    std::puts("PASS: cross-device swapchain collision rejected with ordinary/native cleanup; normal creation unchanged");
}
