#include "android_swapchain_flow.h"
#include "android_native_driver.h"
#include <unistd.h>

const struct RmeAndroidDriverOps *rme_android_driver_ops(void) {
    static const struct RmeAndroidDriverOps ops = {
        dup, close, rme_android_driver_acquire, rme_android_driver_release
    };
    return &ops;
}
