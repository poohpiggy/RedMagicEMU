/* VkNativeBufferANDROID description follows Android's Vulkan native-buffer
 * interface, Copyright 2015 The Android Open Source Project, Apache-2.0. */
#include "android_native_image.h"
#include <vulkan/vk_android_native_buffer.h>
#include "vk_device.h"

VkResult rme_android_native_image_create(VkDevice device, const VkImageCreateInfo *info,
    const struct RmeAndroidNativeImage *buffer, VkImage *image) {
    *image = VK_NULL_HANDLE;
    if (!buffer->handle) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    VK_FROM_HANDLE(vk_device, vk_device, device);
    if (!vk_device->dispatch_table.CreateImage) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkNativeBufferANDROID native = {0};
    native.sType = VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID;
    native.pNext = info->pNext;
    native.handle = (buffer_handle_t)buffer->handle;
    native.stride = buffer->stride;
    native.format = buffer->format;
    native.usage = buffer->usage;
    native.usage2.consumer = buffer->consumer_usage;
    native.usage2.producer = buffer->producer_usage;
    VkImageCreateInfo image_info = *info;
    image_info.pNext = &native;
    return vk_device->dispatch_table.CreateImage(device, &image_info, NULL, image);
}

VkResult rme_android_native_image_destroy(VkDevice device, VkImage image) {
    if (image == VK_NULL_HANDLE) return VK_SUCCESS;
    VK_FROM_HANDLE(vk_device, vk_device, device);
    if (!vk_device->dispatch_table.DestroyImage) return VK_ERROR_EXTENSION_NOT_PRESENT;
    vk_device->dispatch_table.DestroyImage(device, image, NULL);
    return VK_SUCCESS;
}
