// CPU-only test of the actual extracted extension enumeration function.
#include <cassert>
#include <cstdint>
#include <cstring>
#define AEMU_SCOPED_TRACE(...) ((void)0)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#define VK_DEVICE_EXTENSION_COUNT 2
#define VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME "VK_ANDROID_native_buffer"
using VkResult = int;
using VkPhysicalDevice = void*;
struct VkExtensionProperties { char extensionName[256]; uint32_t specVersion; };
struct vk_physical_device { struct { bool extensions[2]; } supported_extensions; };
static VkExtensionProperties vk_device_extensions[2] = {{"VK_ANDROID_native_buffer", 8}, {"VK_EXT_other", 3}};
#define VK_FROM_HANDLE(type, name, value) type *name = reinterpret_cast<type *>(value)
struct Out { VkExtensionProperties *dest; uint32_t *count, capacity, total, written;
    Out(VkExtensionProperties *p, uint32_t *n) : dest(p), count(n), capacity(p ? *n : 0), total(0), written(0) {}
    VkExtensionProperties *next() { total++; if (!dest || written >= capacity) return nullptr; return &dest[written++]; }
};
#define VK_OUTARRAY_MAKE_TYPED(type, name, p, count) Out name(p, count)
#define vk_outarray_append_typed(type, out, prop) for (type *prop = (out)->next(); prop; prop = nullptr)
static VkResult vk_outarray_status(Out *out) { *out->count = out->dest ? out->written : out->total; return out->dest && out->written < out->total ? 5 : 0; }
@FUNCTION@
int main() {
    vk_physical_device device = {{true, true}};
    uint32_t count = 0;
    assert(gfxstream_vk_EnumerateDeviceExtensionProperties(&device, nullptr, &count, nullptr) == 0 && count == 2);
    VkExtensionProperties props[2] = {};
    assert(gfxstream_vk_EnumerateDeviceExtensionProperties(&device, nullptr, &count, props) == 0);
    assert(props[0].specVersion == 7 && props[1].specVersion == 3);
}
