#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace std {
template <typename T> struct vector {
    T values[2];
    unsigned count;
    unsigned size() const { return count; }
    T* data() { return values; }
};
}
using VkResult = int;
using VkInstance = void*;
struct VkAllocationCallbacks {};
struct VkInstanceCreateInfo { uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; };
struct vk_instance { struct { void (*enumerate)(); void (*destroy)(); } physical_devices; };
struct gfxstream_vk_instance { vk_instance vk; int internal_object; };
struct vk_instance_dispatch_table { int unused; };
constexpr VkResult VK_SUCCESS = 0, VK_ERROR_OUT_OF_HOST_MEMORY = -1, VK_ERROR_DEVICE_LOST = -4;
constexpr int VK_SYSTEM_ALLOCATION_SCOPE_OBJECT = 0;
static int scenario, allocations, destroys, seen_count;
static const char* seen_name;
static VkAllocationCallbacks allocator;
static const VkAllocationCallbacks* vk_default_allocator() { return &allocator; }
static void* vk_zalloc(const VkAllocationCallbacks*, unsigned long size, int, int) { ++allocations; return calloc(1, size); }
static void vk_free(const VkAllocationCallbacks*, void* p) { --allocations; free(p); }
static VkResult vk_error(void*, VkResult result) { return result; }
static VkResult SetupInstanceForProcess() { return scenario == 1 ? -10 : VK_SUCCESS; }
static std::vector<const char*> filteredInstanceExtensionNames(uint32_t, const char* const*) { return {{"forwarded", nullptr}, 1}; }
namespace gfxstream { namespace vk {
struct VkEncoder {
    VkResult vkCreateInstance(const VkInstanceCreateInfo* info, const void*, int* object, bool) {
        seen_count = info->enabledExtensionCount;
        seen_name = info->ppEnabledExtensionNames[0];
        *object = 123;
        return scenario == 4 ? -11 : VK_SUCCESS;
    }
    void vkDestroyInstance(int object, const void*, bool) { assert(object == 123); ++destroys; }
};
}}
static gfxstream::vk::VkEncoder encoder;
struct HostConnection {
    static HostConnection* getOrCreate(int) { static HostConnection c; return scenario == 2 ? nullptr : &c; }
    gfxstream::vk::VkEncoder* vkEncoder() { return scenario == 3 ? nullptr : &encoder; }
};
constexpr int kCapsetGfxStreamVulkan = 1;
#define AEMU_SCOPED_TRACE(x) ((void)0)
#define ALOGV(...) ((void)0)
#define VK_HOST_CONNECTION(ret) HostConnection* hostCon = HostConnection::getOrCreate(kCapsetGfxStreamVulkan); gfxstream::vk::VkEncoder* vkEnc = hostCon ? hostCon->vkEncoder() : nullptr; if (!vkEnc) return ret;
static int gfxstream_vk_instance_entrypoints, wsi_instance_entrypoints;
static void vk_instance_dispatch_table_from_entrypoints(vk_instance_dispatch_table*, const int*, bool) {}
static void* get_instance_extensions() { return nullptr; }
static VkResult vk_instance_init(vk_instance*, void*, vk_instance_dispatch_table*, const VkInstanceCreateInfo*, const VkAllocationCallbacks*) { return scenario == 5 ? -12 : VK_SUCCESS; }
static VkInstance gfxstream_vk_instance_to_handle(gfxstream_vk_instance* i) { return i; }
static void gfxstream_vk_enumerate_devices() {}
static void gfxstream_vk_destroy_physical_device() {}

#include "function.inc"

int main(int argc, char** argv) {
    assert(argc == 2);
    const char* cases[] = {"success", "setup", "connection", "encoder", "create", "init", "readonly"};
    for (int i = 0; i != 7; ++i) if (!strcmp(argv[1], cases[i])) scenario = i;
    static const char* names[] = {"original", "mesa-only"};
    static const VkInstanceCreateInfo readonly_info = {2, names};
    VkInstanceCreateInfo writable_info = readonly_info;
    const VkInstanceCreateInfo* info = scenario == 6 ? &readonly_info : &writable_info;
    VkInstance output = nullptr;
    VkResult result = gfxstream_vk_CreateInstance(info, nullptr, &output);
    assert(info->enabledExtensionCount == 2 && info->ppEnabledExtensionNames == names);
    if (scenario == 0 || scenario == 6) {
        assert(result == VK_SUCCESS && output && allocations == 1 && seen_count == 1 && !strcmp(seen_name, "forwarded"));
        vk_free(&allocator, output);
    } else {
        assert(result != VK_SUCCESS && output == nullptr && allocations == 0);
        assert(destroys == (scenario == 5));
    }
    return 0;
}
