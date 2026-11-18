#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

using VkDevice = uint64_t;
using VkImage = uint64_t;
using VkImageView = uint64_t;
using VkResult = int;
constexpr VkResult VK_SUCCESS = 0, VK_ERROR_OUT_OF_HOST_MEMORY = -1, VK_ERROR_UNKNOWN = -13;
struct VkAllocationCallbacks {};
struct VkImageViewCreateInfo { VkImage image; int format; struct { int baseMipLevel; } subresourceRange; };
namespace android { namespace base {
struct BumpPool {};
template<class Map, class Key> auto find(Map& map, Key key) -> typename Map::mapped_type* {
    auto it = map.find(key);
    return it == map.end() ? nullptr : &it->second;
}
}}
struct CompressedImageInfo {
    VkImage outputImage() const { return 0; }
    VkImage compressedMipmap(int) const { return 0; }
    static int getOutputFormat(int format) { return format; }
    static int getCompressedMipmapsFormat(int format) { return format; }
    static bool needEmulatedAlpha(int) { return false; }
};
struct NativeInfo { bool externallyBacked = false; };
struct ImageInfo { VkDevice device; CompressedImageInfo cmpInfo; std::shared_ptr<NativeInfo> anbInfo; std::optional<int> boundColorBuffer; };
struct DeviceInfo {
    bool needEmulatedDecompression(int) { return false; }
    bool needEmulatedDecompression(const CompressedImageInfo&) { return false; }
    struct { void addDebugLabel(VkImageView, const char*, int) {} } debugUtilsHelper;
};
struct ViewInfo { VkDevice device; bool needEmulatedAlpha; std::optional<int> boundColorBuffer; };
static int driverCalls;
struct Driver {
    VkResult vkCreateImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* view) {
        ++driverCalls; *view = 0x44; return VK_SUCCESS;
    }
} driver;
static VkDevice unbox_VkDevice(VkDevice device) { return device; }
static Driver* dispatch_VkDevice(VkDevice) { return &driver; }
static VkImageView new_boxed_non_dispatchable_VkImageView(VkImageView view) { return view + 1; }
struct Handler {
    std::recursive_mutex mLock;
    std::unordered_map<VkDevice, DeviceInfo> mDeviceInfo;
    std::unordered_map<VkImage, ImageInfo> mImageInfo;
    std::unordered_map<VkImageView, ViewInfo> mImageViewInfo;
    /* EXTRACTED_METHOD */
};
int main() {
    Handler h;
    h.mDeviceInfo.emplace(1, DeviceInfo{});
    h.mDeviceInfo.emplace(2, DeviceInfo{});
    h.mImageInfo.emplace(7, ImageInfo{1, {}, {}, {}});
    VkImageViewCreateInfo ci{7, 0, {0}};
    VkImageView view = 0;
    if (h.on_vkCreateImageView(nullptr, 1, &ci, nullptr, &view) != VK_SUCCESS || driverCalls != 1 || view != 0x45) return 1;
    view = 0;
    if (h.on_vkCreateImageView(nullptr, 2, &ci, nullptr, &view) != VK_ERROR_UNKNOWN || driverCalls != 1 || view != 0) return 2;
    ci.image = 9;
    if (h.on_vkCreateImageView(nullptr, 1, &ci, nullptr, &view) != VK_ERROR_OUT_OF_HOST_MEMORY || driverCalls != 1) return 3;
    std::puts("PASS: matching device dispatches; wrong-device image never reaches driver");
}
