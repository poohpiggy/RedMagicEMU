#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using VkDevice = uint64_t; using VkSwapchainKHR = uint64_t; using VkImage = uint64_t;
using VkFormat = uint32_t; using VkImageUsageFlags = uint32_t; using VkSwapchainCreateFlagsKHR = uint32_t;
using VkSharingMode = uint32_t; using VkResult = int;
constexpr VkResult VK_SUCCESS = 0, VK_INCOMPLETE = 5, VK_ERROR_UNKNOWN = -13;
constexpr auto VK_SHARING_MODE_CONCURRENT = 1U;
constexpr auto VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14U;
constexpr auto VK_IMAGE_TYPE_2D = 1U;
constexpr auto VK_SAMPLE_COUNT_1_BIT = 1U;
constexpr auto VK_IMAGE_TILING_OPTIMAL = 0U;
constexpr auto VK_IMAGE_LAYOUT_UNDEFINED = 0U;
struct VkExtent2D { uint32_t width, height; };
struct VkExtent3D { uint32_t width, height, depth; };
struct VkSwapchainCreateInfoKHR {
    const void* pNext; VkSwapchainCreateFlagsKHR flags; VkFormat imageFormat; VkExtent2D imageExtent;
    uint32_t imageArrayLayers; VkImageUsageFlags imageUsage; VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices;
};
struct VkImageCreateInfo {
    uint32_t sType = 0, imageType = 0, format = 0;
    VkExtent3D extent{};
    uint32_t mipLevels = 0, arrayLayers = 0, samples = 0, tiling = 0, usage = 0, sharingMode = 0;
    uint32_t queueFamilyIndexCount = 0; const uint32_t* pQueueFamilyIndices = nullptr;
    uint32_t initialLayout = 0;
};
struct VkAllocationCallbacks {};
struct VulkanDispatch {
    int destroyCount = 0; VkResult result = VK_SUCCESS; std::vector<VkImage> returned;
    void vkDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) { ++destroyCount; }
    VkResult vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR, uint32_t* count, VkImage* images) {
        *count = static_cast<uint32_t>(returned.size());
        if (images) for (size_t i = 0; i < returned.size(); ++i) images[i] = returned[i];
        return result;
    }
};
struct CompressedImageInfo {
    CompressedImageInfo() = default; explicit CompressedImageInfo(VkDevice) {}
    VkImage outputImage() const { return 0; } void destroy(VulkanDispatch*) {}
};
struct ImageInfo {
    VkDevice device; VkImageCreateInfo imageCreateInfoShallow{};
    std::shared_ptr<int> anbInfo; CompressedImageInfo cmpInfo; uint32_t layout = 0;
};
struct DeviceInfo { bool compressed = false; bool needEmulatedDecompression(VkFormat) const { return compressed; } };
namespace android { namespace base {
template<class Map, class Key> auto find(Map& map, Key key) -> typename Map::mapped_type* {
    auto it = map.find(key); return it == map.end() ? nullptr : &it->second;
}
}}
static std::unordered_map<VkImage, VkImage> boxes;
static VkImage nextBox = 10000;
static int boxCalls = 0, failBoxAt = -1;
static VkImage new_boxed_non_dispatchable_VkImage(VkImage raw) {
    if (++boxCalls == failBoxAt) return 0;
    return boxes[raw] = nextBox++;
}
static VkImage unboxed_to_boxed_non_dispatchable_VkImage(VkImage raw) { auto it = boxes.find(raw); return it == boxes.end() ? 0 : it->second; }
static void delete_VkImage(VkImage boxed) { for (auto it = boxes.begin(); it != boxes.end(); ++it) if (it->second == boxed) { boxes.erase(it); return; } }
struct Handler {
    /* EXTRACTED_METHODS */
    /* EXTRACTED_DESTROY */
    VkResult exerciseDecoder(VulkanDispatch* vk, VkDevice unboxed_device, VkSwapchainKHR boxed_swapchain,
                             uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) {
        auto* m_state = this;
        auto swapchain = boxed_swapchain;
        /* EXTRACTED_DISPATCH */
return vkGetSwapchainImagesKHR_VkResult_return;
    }
    bool snapshots = false;
    bool snapshotsEnabled() const { return snapshots; }
    std::recursive_mutex mLock;
    std::unordered_map<VkSwapchainKHR, SwapchainImageMetadata> mSwapchainImageMetadata;
    std::unordered_map<VkImage, VkSwapchainKHR> mSwapchainImageOwners;
    std::unordered_map<VkImage, ImageInfo> mImageInfo;
    std::unordered_map<VkDevice, DeviceInfo> mDeviceInfo;
};
int main() {
    Handler h; h.mDeviceInfo.emplace(1, DeviceInfo{}); h.mDeviceInfo.emplace(2, DeviceInfo{});
    VkSwapchainCreateInfoKHR info{nullptr, 0, 44, {640, 480}, 1, 17, 0, 0, nullptr};
    h.on_recordSwapchain(1, 10, &info);
    VulkanDispatch driver; driver.result = VK_INCOMPLETE; driver.returned = {111};
    uint32_t count = 1; VkImage incomplete[] = {0};
    if (h.exerciseDecoder(&driver, 1, 10, &count, incomplete) != VK_INCOMPLETE || count != 1 || incomplete[0] == 111) return 1;
    const VkImage firstBox = incomplete[0];
    driver.result = VK_SUCCESS; driver.returned = {111, 222}; count = 2;
    VkImage full[] = {0, 0};
    if (h.exerciseDecoder(&driver, 1, 10, &count, full) != VK_SUCCESS || count != 2 || full[0] != firstBox || full[1] == 222) return 2;
    if (h.mImageInfo.at(111).device != 1 || h.mImageInfo.at(111).imageCreateInfoShallow.extent.width != 640) return 3;
    VkImage repeated[] = {222, 111};
    if (h.on_registerSwapchainImages(1, 10, 2, repeated) != VK_SUCCESS || repeated[0] != full[1] || repeated[1] != firstBox) return 4;
    if (h.on_validateSwapchain(2, 10) == VK_SUCCESS) return 5;
    h.on_recordSwapchain(2, 20, &info);
    driver.returned = {333, 111}; count = 2; VkImage conflict[] = {0, 0};
    if (h.exerciseDecoder(&driver, 2, 20, &count, conflict) != VK_ERROR_UNKNOWN || count != 0 || boxes.count(333)) return 6;
    info.pNext = reinterpret_cast<void*>(1);
    h.on_recordSwapchain(1, 30, &info);
    if (h.on_validateSwapchain(1, 30) == VK_SUCCESS) return 7;
    info.pNext = nullptr;
    h.on_recordSwapchain(1, 40, &info);
    h.snapshots = true;
    if (h.on_validateSwapchain(1, 40) == VK_SUCCESS) return 8;
    h.snapshots = false;
    h.mDeviceInfo.at(1).compressed = true;
    if (h.on_validateSwapchain(1, 40) == VK_SUCCESS) return 9;
    h.mDeviceInfo.at(1).compressed = false;
    VkImage failed[] = {444, 555};
    failBoxAt = boxCalls + 2;
    if (h.on_registerSwapchainImages(1, 40, 2, failed) == VK_SUCCESS || boxes.count(444) ||
        boxes.count(555) || h.mImageInfo.count(444) || !h.mSwapchainImageMetadata.at(40).images.empty()) return 10;
    failBoxAt = -1;
    h.destroyImageLocked(1, &driver, 111, nullptr);
    if (driver.destroyCount || !h.mImageInfo.count(111)) return 11;
    h.on_forgetSwapchain(1, 10);
    if (driver.destroyCount || h.mImageInfo.count(111) || h.mImageInfo.count(222) || boxes.count(111) || boxes.count(222)) return 12;
    std::puts("PASS: stable boxes, incomplete enumeration, collisions, unsupported metadata, and driver-owned image cleanup");
}
