/* CPU-only Vulkan dispatch test; native handles and images are synthetic. */
#include "android_native_image.h"
#include <vulkan/vk_android_native_buffer.h>
#include "vk_device.h"
#include <assert.h>
#include <stdint.h>

static int created, destroyed;
static int sentinel;
static VkResult backend_result = VK_SUCCESS;
static VKAPI_ATTR VkResult VKAPI_CALL create_image(VkDevice device, const VkImageCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkImage *image) {
    assert(device && !allocator && info && info->pNext);
    const VkNativeBufferANDROID *native = info->pNext;
    assert(native->sType == VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID);
    assert(native->pNext == &sentinel);
    assert((uintptr_t)native->handle == 7 && native->stride == 64 && native->format == 1);
    assert(native->usage == 5 && native->usage2.consumer == 6 && native->usage2.producer == 7 && native->usage3 == 0);
    created++;
    if (backend_result == VK_SUCCESS) *image = (VkImage)(uintptr_t)9;
    return backend_result;
}
static VKAPI_ATTR void VKAPI_CALL destroy_image(VkDevice device, VkImage image, const VkAllocationCallbacks *allocator) {
    assert(device && (uintptr_t)image == 9 && !allocator); destroyed++;
}
int main(void) {
    struct vk_device device = {0};
    device.base.type = VK_OBJECT_TYPE_DEVICE;
    VkImageCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = &sentinel;
    struct RmeAndroidNativeImage native = {(void *)(uintptr_t)7, 64, 1, 5, 6, 7};
    VkImage image = VK_NULL_HANDLE;
    assert(rme_android_native_image_create((VkDevice)&device, &info, &native, &image) == VK_ERROR_EXTENSION_NOT_PRESENT);
    device.dispatch_table.CreateImage = create_image;
    device.dispatch_table.DestroyImage = destroy_image;
    backend_result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
    assert(rme_android_native_image_create((VkDevice)&device, &info, &native, &image) == VK_ERROR_INVALID_EXTERNAL_HANDLE);
    assert(image == VK_NULL_HANDLE && created == 1);
    backend_result = VK_SUCCESS;
    assert(rme_android_native_image_create((VkDevice)&device, &info, &native, &image) == VK_SUCCESS);
    assert((uintptr_t)image == 9 && created == 2);
    assert(rme_android_native_image_destroy((VkDevice)&device, image) == VK_SUCCESS);
    assert(destroyed == 1);
    native.handle = NULL;
    assert(rme_android_native_image_create((VkDevice)&device, &info, &native, &image) == VK_ERROR_INVALID_EXTERNAL_HANDLE);
    assert(image == VK_NULL_HANDLE && created == 2);
}
