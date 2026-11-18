#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using VkResult = int;
using VkInstance = uint64_t;
using VkAllocationCallbacks = int;
namespace android { namespace base { struct BumpPool {}; }}
constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_INCOMPLETE = 5;
constexpr VkResult VK_ERROR_INITIALIZATION_FAILED = -3;
constexpr VkResult VK_ERROR_EXTENSION_NOT_PRESENT = -7;
constexpr VkResult VK_ERROR_OUT_OF_HOST_MEMORY = -1;
#define VK_KHR_SURFACE_EXTENSION_NAME "VK_KHR_surface"
#define VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME "VK_KHR_external_memory_capabilities"
#define VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME "VK_KHR_external_semaphore_capabilities"
#define VK_EXT_DEBUG_UTILS_EXTENSION_NAME "VK_EXT_debug_utils"
struct VkExtensionProperties { char extensionName[256]; uint32_t specVersion; };
struct VkInstanceCreateInfo { uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; };
static std::vector<std::string> available;
static std::vector<std::string> created;
static int calls = 0;
static VkResult enumerationResult = VK_SUCCESS;
static VkResult enumerate(const char*, uint32_t* count, VkExtensionProperties* out) {
    ++calls;
    if (enumerationResult != VK_SUCCESS) return enumerationResult;
    if (!out) { *count = static_cast<uint32_t>(available.size()); return VK_SUCCESS; }
    uint32_t n = std::min(*count, static_cast<uint32_t>(available.size()));
    for (uint32_t i = 0; i < n; ++i) {
        std::strncpy(out[i].extensionName, available[i].c_str(), sizeof(out[i].extensionName) - 1);
        out[i].extensionName[sizeof(out[i].extensionName) - 1] = 0;
    }
    *count = n;
    return n < available.size() ? VK_INCOMPLETE : VK_SUCCESS;
}
struct Dispatch { decltype(&enumerate) vkEnumerateInstanceExtensionProperties = enumerate; };
struct Emu {
    bool instanceSupportsExternalMemoryCapabilities = false;
    bool instanceSupportsExternalSemaphoreCapabilities = false;
    bool debugUtilsAvailableAndRequested = false;
    bool instanceSupportsSurface = false;
};
struct TestState {
    Emu emu;
    Emu* m_emu = &emu;
    Dispatch dispatch;
    Dispatch* m_vk = &dispatch;
    bool isEmulatedInstanceExtension(const char*) { return false; }
#include "methods.inc"
};
static size_t count(const char* name) { return std::count(created.begin(), created.end(), name); }
static VkResult create(TestState& state, std::vector<const char*> names) {
    created.clear();
    VkInstanceCreateInfo ci{static_cast<uint32_t>(names.size()), names.data()};
    return state.on_vkCreateInstance(nullptr, &ci, nullptr, nullptr);
}
int main() {
    const char* android = "VK_KHR_android_surface";
    const char* surface = VK_KHR_SURFACE_EXTENSION_NAME;
    TestState state;
    available = {surface, android};
    state.emu.instanceSupportsSurface = true;
    assert(create(state, {surface, android, surface}) == VK_SUCCESS);
    assert(count(surface) == 1 && count(android) == 1 && calls == 2);
    available = {surface}; calls = 0;
    assert(create(state, {android}) == VK_ERROR_EXTENSION_NOT_PRESENT && created.empty());
    available = {android}; calls = 0;
    assert(create(state, {android}) == VK_ERROR_EXTENSION_NOT_PRESENT && created.empty());
    available = {surface, android}; calls = 0; state.emu.instanceSupportsSurface = false;
    assert(create(state, {android}) == VK_SUCCESS);
    assert(count(surface) == 1 && count(android) == 1 && calls == 2);
    enumerationResult = VK_ERROR_OUT_OF_HOST_MEMORY; calls = 0;
    assert(create(state, {android}) == VK_ERROR_OUT_OF_HOST_MEMORY && created.empty());
    enumerationResult = VK_INCOMPLETE; calls = 0;
    assert(create(state, {android}) == VK_ERROR_INITIALIZATION_FAILED && created.empty());
    enumerationResult = VK_ERROR_OUT_OF_HOST_MEMORY; calls = 0;
    assert(create(state, {"VK_EXT_example"}) == VK_SUCCESS && calls == 0);
}
