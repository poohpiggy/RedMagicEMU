#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>

using VkDevice = uint64_t;
using VkSwapchainKHR = uint64_t;
using VkFormat = uint32_t;
using VkImageUsageFlags = uint32_t;
using VkSwapchainCreateFlagsKHR = uint32_t;
using VkSharingMode = uint32_t;
constexpr VkSharingMode VK_SHARING_MODE_CONCURRENT = 1;
struct VkExtent2D { uint32_t width, height; };
struct VkSwapchainCreateInfoKHR {
    const void* pNext;
    VkSwapchainCreateFlagsKHR flags;
    VkFormat imageFormat;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkImageUsageFlags imageUsage;
    VkSharingMode imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t* pQueueFamilyIndices;
};
struct Handler {
    /* EXTRACTED_METHODS */
    std::recursive_mutex mLock;
    std::unordered_map<VkSwapchainKHR, SwapchainImageMetadata> mSwapchainImageMetadata;
};
int main() {
    Handler h;
    uint32_t families[] = {2, 7};
    VkSwapchainCreateInfoKHR info{reinterpret_cast<void*>(1), 3, 44, {1280, 720}, 2, 17, VK_SHARING_MODE_CONCURRENT, 2, families};
    h.on_recordSwapchain(10, 101, &info);
    families[0] = 99;
    const auto& saved = h.mSwapchainImageMetadata.at(101);
    if (saved.device != 10 || saved.flags != 3 || saved.format != 44 || saved.extent.width != 1280 ||
        saved.extent.height != 720 || saved.arrayLayers != 2 || saved.usage != 17 ||
        saved.sharingMode != VK_SHARING_MODE_CONCURRENT || !saved.hasPNext ||
        saved.queueFamilyIndices.size() != 2 || saved.queueFamilyIndices[0] != 2 || saved.queueFamilyIndices[1] != 7) return 1;
    info.pNext = nullptr;
    h.on_recordSwapchain(20, 102, &info);
    h.on_forgetSwapchain(20, 101);
    if (h.mSwapchainImageMetadata.count(101) != 1) return 2;
    h.on_forgetSwapchain(10, 101);
    if (h.mSwapchainImageMetadata.count(101) || h.mSwapchainImageMetadata.count(102) != 1) return 3;
    h.on_recordSwapchain(10, 103, &info);
    h.forgetDeviceSwapchainsLocked(10);
    if (h.mSwapchainImageMetadata.count(103) || h.mSwapchainImageMetadata.count(102) != 1) return 4;
    info.imageSharingMode = 0;
    h.on_recordSwapchain(30, 104, &info);
    if (!h.mSwapchainImageMetadata.at(104).queueFamilyIndices.empty()) return 5;
    h.mSwapchainImageMetadata.clear();
    if (!h.mSwapchainImageMetadata.empty()) return 6;
    std::puts("PASS: swapchain device and image metadata; independent queue-family copy; scoped cleanup");
}
