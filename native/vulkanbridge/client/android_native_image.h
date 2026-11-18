/* Internal VkImage creation for a supplied, real Android native buffer. */
#ifndef RME_ANDROID_NATIVE_IMAGE_H
#define RME_ANDROID_NATIVE_IMAGE_H
#include <stdint.h>
#include <vulkan/vulkan_core.h>

struct RmeAndroidNativeImage {
    const void *handle;
    int stride;
    int format;
    int usage;
    uint64_t consumer_usage;
    uint64_t producer_usage;
};
/* No handle/id is generated here; usage2 must match the real buffer. The d7a
 * wire protocol reports native-buffer spec 7 and does not serialize usage3. */
VkResult rme_android_native_image_create(VkDevice device, const VkImageCreateInfo *info,
    const struct RmeAndroidNativeImage *buffer, VkImage *image);
VkResult rme_android_native_image_destroy(VkDevice device, VkImage image);
#endif
