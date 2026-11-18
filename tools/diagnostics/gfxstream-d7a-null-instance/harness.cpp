/* Exercises the actual extracted gfxstream GetInstanceProcAddr body. */
#include <string.h>

using PFN_vkVoidFunction = void (*)();
using VkInstance = void*;
struct vk_instance { int marker; };
struct gfxstream_vk_instance { vk_instance vk; };
struct fake_entrypoints { int marker; };
static fake_entrypoints gfxstream_vk_instance_entrypoints{1};
static int called;
static const vk_instance* seen;
static void result_fn() {}
static PFN_vkVoidFunction vk_instance_get_proc_addr(const vk_instance* instance,
                                                    const fake_entrypoints* entrypoints,
                                                    const char* name) {
    ++called;
    seen = instance;
    if (entrypoints != &gfxstream_vk_instance_entrypoints || !name) return nullptr;
    if (!instance && strcmp(name, "vkCreateInstance") == 0) return result_fn;
    if (instance && instance->marker == 42 && strcmp(name, "vkDestroyInstance") == 0)
        return result_fn;
    return nullptr;
}
#define VK_FROM_HANDLE(type, name, handle) \
    struct type* name = static_cast<struct type*>(handle)
#include "function.h"

int main() {
    if (gfxstream_vk_GetInstanceProcAddr(nullptr, "vkCreateInstance") != result_fn ||
        called != 1 || seen != nullptr) return 1;
    if (gfxstream_vk_GetInstanceProcAddr(nullptr, "vkDestroyInstance") != nullptr ||
        called != 2 || seen != nullptr) return 2;
    gfxstream_vk_instance instance{{42}};
    if (gfxstream_vk_GetInstanceProcAddr(&instance, "vkDestroyInstance") != result_fn ||
        called != 3 || seen != &instance.vk) return 3;
    return 0;
}
