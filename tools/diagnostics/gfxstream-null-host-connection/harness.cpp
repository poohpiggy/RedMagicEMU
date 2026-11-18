// Exercises the VK_HOST_CONNECTION macro copied from the selected gfxstream source.
#include <string.h>

namespace gfxstream { namespace vk { struct VkEncoder {}; } }
static int lookups, encoder_calls, log_calls, body_calls;
static gfxstream::vk::VkEncoder encoder;
static int payload;
static bool host_missing, encoder_missing;
constexpr int kCapsetGfxStreamVulkan = 7;

struct HostConnection {
    gfxstream::vk::VkEncoder* value;
    static HostConnection* getOrCreate(int capset) {
        ++lookups;
        static HostConnection c;
        if (capset != kCapsetGfxStreamVulkan || host_missing) return nullptr;
        c.value = encoder_missing ? nullptr : &encoder;
        return &c;
    }
    gfxstream::vk::VkEncoder* vkEncoder() { ++encoder_calls; return value; }
};
static void mesa_loge(const char*) { ++log_calls; }

#include "macro.h"

static int int_result() {
    VK_HOST_CONNECTION(-4);
    ++body_calls;
    return vkEnc == &encoder ? 23 : 24;
}
static int* pointer_result() {
    VK_HOST_CONNECTION(nullptr);
    ++body_calls;
    return vkEnc == &encoder ? &payload : nullptr;
}
static void void_result() {
    VK_HOST_CONNECTION();
    if (vkEnc == &encoder) ++body_calls;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    host_missing = strcmp(argv[1], "host-missing") == 0;
    encoder_missing = strcmp(argv[1], "encoder-missing") == 0;
    const bool success = strcmp(argv[1], "ok") == 0;
    if (!host_missing && !encoder_missing && !success) return 2;
    if (strcmp(argv[2], "int") == 0) {
        if (int_result() != (success ? 23 : -4)) return 3;
    } else if (strcmp(argv[2], "pointer") == 0) {
        if (pointer_result() != (success ? &payload : nullptr)) return 4;
    } else if (strcmp(argv[2], "void") == 0) {
        void_result();
    } else return 2;
    if (lookups != 1 || encoder_calls != (host_missing ? 0 : 1) ||
        log_calls != (success ? 0 : 1) || body_calls != (success ? 1 : 0)) return 5;
    return 0;
}
