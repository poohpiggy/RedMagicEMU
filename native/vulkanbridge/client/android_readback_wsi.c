/* Experimental presentation through the Guest's Android window. */
#include "android_readback_wsi.h"
#include "android_surface.h"
#include "vk_device.h"
#include "vk_physical_device.h"
#include "vk_queue.h"

#include <android/native_window.h>
#include <arm_neon.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <system/window.h>
#ifdef RME_TRACE_VULKAN_PROCS
#include <stdio.h>
#include <unistd.h>
#endif

enum { MIN_IMAGES = 2, MAX_IMAGES = 8 };
static const VkImageUsageFlags usages = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
    VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
static const VkFormat formats[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB };

struct ImageSlot {
    VkImage image;
    VkDeviceMemory memory;
    VkCommandBuffer readback;
    bool acquired;
};

struct Chain {
    struct Chain *next;
    struct ANativeWindow *window;
    VkDevice device;
    VkQueue acquire_queue;
    VkAllocationCallbacks allocator;
    VkExtent2D extent;
    VkFormat format;
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    void *pixels;
    VkFence complete;
    VkCommandPool pool;
    uint32_t family;
    uint32_t count;
    uint32_t next_slot;
    bool retired;
    bool mailbox;
    struct ImageSlot slots[MAX_IMAGES];
};

static pthread_mutex_t chains_lock = PTHREAD_MUTEX_INITIALIZER;
static struct Chain *chains;

static struct Chain *find_chain(VkSwapchainKHR handle) {
    struct Chain *chain = chains;
    while (chain && (VkSwapchainKHR)(uintptr_t)chain != handle) chain = chain->next;
    return chain;
}

static struct Chain *lookup_chain(VkSwapchainKHR handle) {
    pthread_mutex_lock(&chains_lock);
    struct Chain *chain = find_chain(handle);
    pthread_mutex_unlock(&chains_lock);
    return chain;
}

VkResult rme_readback_support(VkPhysicalDevice physical, uint32_t family, VkSurfaceKHR surface, VkBool32 *out) {
    *out = VK_FALSE;
    if (!rme_android_surface_window_vk(surface)) return VK_ERROR_SURFACE_LOST_KHR;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    if (family >= count) return VK_SUCCESS;
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    if (!families) return VK_ERROR_OUT_OF_HOST_MEMORY;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    *out = family < count && families[family].queueCount && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT);
    free(families);
    return VK_SUCCESS;
}

VkResult rme_readback_capabilities(VkPhysicalDevice physical, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *out) {
    struct ANativeWindow *window = rme_android_surface_window_vk(surface);
    if (!window) return VK_ERROR_SURFACE_LOST_KHR;
    int width = ANativeWindow_getWidth(window), height = ANativeWindow_getHeight(window);
    if (width < 1 || height < 1) return VK_ERROR_SURFACE_LOST_KHR;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    const uint32_t max = props.limits.maxImageDimension2D;
    *out = (VkSurfaceCapabilitiesKHR){
        .minImageCount = MIN_IMAGES, .maxImageCount = MAX_IMAGES,
        .currentExtent = {(uint32_t)width, (uint32_t)height},
        .minImageExtent = {1, 1}, .maxImageExtent = {max, max}, .maxImageArrayLayers = 1,
        .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .supportedUsageFlags = usages,
    };
    return VK_SUCCESS;
}

VkResult rme_readback_formats(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t *count, VkSurfaceFormatKHR *out) {
    if (!rme_android_surface_window_vk(surface)) return VK_ERROR_SURFACE_LOST_KHR;
    const uint32_t capacity = out ? *count : UINT32_MAX;
    uint32_t found = 0;
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        VkImageFormatProperties props;
        if (vkGetPhysicalDeviceImageFormatProperties(physical, formats[i], VK_IMAGE_TYPE_2D,
                VK_IMAGE_TILING_OPTIMAL, usages, 0, &props) != VK_SUCCESS) continue;
        if (out && found < capacity) out[found] = (VkSurfaceFormatKHR){formats[i], VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        ++found;
    }
    *count = found < capacity ? found : capacity;
    return found > capacity ? VK_INCOMPLETE : VK_SUCCESS;
}

static bool supports_mailbox(const struct ANativeWindow *window) {
    return window->setSwapInterval && window->minSwapInterval == 0;
}

VkResult rme_readback_modes(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t *count, VkPresentModeKHR *out) {
    (void)physical;
    struct ANativeWindow *window = rme_android_surface_window_vk(surface);
    if (!window) return VK_ERROR_SURFACE_LOST_KHR;
    const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
    const uint32_t available = supports_mailbox(window) ? 2 : 1;
    const uint32_t written = out && *count < available ? *count : available;
    for (uint32_t i = 0; out && i < written; ++i) out[i] = modes[i];
    *count = written;
    return written < available ? VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult allocate_memory(VkDevice device, VkPhysicalDevice physical, VkMemoryRequirements req,
    VkMemoryPropertyFlags required, VkDeviceMemory *out) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if (!(req.memoryTypeBits & (1u << i)) || (props.memoryTypes[i].propertyFlags & required) != required) continue;
        VkMemoryAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size, .memoryTypeIndex = i};
        return vkAllocateMemory(device, &info, NULL, out);
    }
#ifdef RME_TRACE_VULKAN_PROCS
    fprintf(stderr, "RME_VK_SWAPCHAIN_MEMORY_UNSUPPORTED bits=%x required=%x pid=%d tid=%d\n",
        req.memoryTypeBits, required, getpid(), gettid());
#endif
    return VK_ERROR_FEATURE_NOT_PRESENT;
}

static void free_chain(struct Chain *chain) {
    VkDevice device = chain->device;
    if (chain->pool) vkDestroyCommandPool(device, chain->pool, NULL);
    if (chain->complete) vkDestroyFence(device, chain->complete, NULL);
    if (chain->pixels) vkUnmapMemory(device, chain->staging_memory);
    if (chain->staging) vkDestroyBuffer(device, chain->staging, NULL);
    if (chain->staging_memory) vkFreeMemory(device, chain->staging_memory, NULL);
    for (uint32_t i = 0; i < chain->count; ++i) {
        if (chain->slots[i].image) vkDestroyImage(device, chain->slots[i].image, NULL);
        if (chain->slots[i].memory) vkFreeMemory(device, chain->slots[i].memory, NULL);
    }
    ANativeWindow_release(chain->window);
    chain->allocator.pfnFree(chain->allocator.pUserData, chain);
}

static VkResult create_images(struct Chain *chain, const VkSwapchainCreateInfoKHR *info, VkPhysicalDevice physical) {
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = chain->format, .extent = {chain->extent.width, chain->extent.height, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = info->imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .sharingMode = info->imageSharingMode,
        .queueFamilyIndexCount = info->queueFamilyIndexCount, .pQueueFamilyIndices = info->pQueueFamilyIndices};
    for (uint32_t i = 0; i < chain->count; ++i) {
        struct ImageSlot *slot = &chain->slots[i];
        VkResult result = vkCreateImage(chain->device, &image, NULL, &slot->image);
        if (result != VK_SUCCESS) return result;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(chain->device, slot->image, &req);
        result = allocate_memory(chain->device, physical, req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &slot->memory);
        if (result != VK_SUCCESS) return result;
        result = vkBindImageMemory(chain->device, slot->image, slot->memory, 0);
        if (result != VK_SUCCESS) return result;
    }
    VkBufferCreateInfo staging = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = (VkDeviceSize)chain->extent.width * chain->extent.height * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkResult result = vkCreateBuffer(chain->device, &staging, NULL, &chain->staging);
    if (result != VK_SUCCESS) return result;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(chain->device, chain->staging, &req);
    result = allocate_memory(chain->device, physical, req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &chain->staging_memory);
    if (result != VK_SUCCESS) return result;
    result = vkBindBufferMemory(chain->device, chain->staging, chain->staging_memory, 0);
    if (result != VK_SUCCESS) return result;
    return vkMapMemory(chain->device, chain->staging_memory, 0, VK_WHOLE_SIZE, 0, &chain->pixels);
}

VkResult rme_readback_create(VkDevice device, const VkSwapchainCreateInfoKHR *info,
    const VkAllocationCallbacks *allocator, VkSwapchainKHR *out) {
    *out = VK_NULL_HANDLE;
    struct Chain *old = lookup_chain(info->oldSwapchain);
    if (old) old->retired = true;
    struct ANativeWindow *window = rme_android_surface_window_vk(info->surface);
    if (!window) return VK_ERROR_SURFACE_LOST_KHR;
    const bool mailbox = info->presentMode == VK_PRESENT_MODE_MAILBOX_KHR && supports_mailbox(window);
    if (info->pNext || info->flags || info->imageArrayLayers != 1 || info->minImageCount < MIN_IMAGES ||
        info->minImageCount > MAX_IMAGES || info->imageUsage & ~usages ||
        (!mailbox && info->presentMode != VK_PRESENT_MODE_FIFO_KHR) ||
        info->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR || info->preTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ||
        info->compositeAlpha != VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
#ifdef RME_TRACE_VULKAN_PROCS
        const VkBaseInStructure *next = info->pNext;
        fprintf(stderr, "RME_VK_SWAPCHAIN_UNSUPPORTED next=%d flags=%x layers=%u count=%u usage=%x "
            "mode=%d color=%d transform=%x alpha=%x pid=%d tid=%d\n",
            next ? (int)next->sType : -1, info->flags, info->imageArrayLayers, info->minImageCount,
            info->imageUsage, info->presentMode, info->imageColorSpace, info->preTransform, info->compositeAlpha,
            getpid(), gettid());
#endif
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    VK_FROM_HANDLE(vk_device, vk_dev, device);
    VkPhysicalDevice physical = vk_physical_device_to_handle(vk_dev->physical);
    VkSurfaceCapabilitiesKHR caps;
    VkResult result = rme_readback_capabilities(physical, info->surface, &caps);
    if (result != VK_SUCCESS) return result;
    if (!info->imageExtent.width || !info->imageExtent.height ||
        info->imageExtent.width > caps.maxImageExtent.width || info->imageExtent.height > caps.maxImageExtent.height)
        return VK_ERROR_OUT_OF_DATE_KHR;
    VkSurfaceFormatKHR supported[sizeof(formats) / sizeof(formats[0])];
    uint32_t format_count = sizeof(supported) / sizeof(supported[0]);
    result = rme_readback_formats(physical, info->surface, &format_count, supported);
    if (result != VK_SUCCESS) return result;
    bool found = false;
    for (uint32_t i = 0; i < format_count; ++i) found |= supported[i].format == info->imageFormat;
    if (!found) return VK_ERROR_FORMAT_NOT_SUPPORTED;

    VkQueue queue = VK_NULL_HANDLE;
    if (list_is_empty(&vk_dev->queues)) {
        /* gfxstream creates queue handles lazily, so the caller must retrieve the render queue first. */
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    struct vk_queue *first = list_first_entry(&vk_dev->queues, struct vk_queue, link);
    queue = vk_queue_to_handle(first);
    if (!allocator) allocator = &vk_dev->alloc;
    struct Chain *chain = allocator->pfnAllocation(allocator->pUserData, sizeof(*chain),
        _Alignof(struct Chain), VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    if (!chain) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *chain = (struct Chain){.allocator = *allocator, .device = device, .window = window,
        .acquire_queue = queue, .extent = info->imageExtent, .format = info->imageFormat, .count = info->minImageCount};
    ANativeWindow_acquire(window);
    result = create_images(chain, info, physical);
    if (result == VK_SUCCESS && ANativeWindow_setBuffersGeometry(window,
            chain->extent.width, chain->extent.height, WINDOW_FORMAT_RGBA_8888) != 0)
        result = VK_ERROR_SURFACE_LOST_KHR;
    /* Android uses interval zero to let the window producer replace pending buffers. */
    if (result == VK_SUCCESS && supports_mailbox(window) && window->setSwapInterval(window, mailbox ? 0 : 1) != 0)
        result = VK_ERROR_SURFACE_LOST_KHR;
    if (result != VK_SUCCESS) {
        free_chain(chain);
        return result;
    }
    chain->mailbox = mailbox;
    pthread_mutex_lock(&chains_lock);
    chain->next = chains;
    chains = chain;
    pthread_mutex_unlock(&chains_lock);
    *out = (VkSwapchainKHR)(uintptr_t)chain;
    return VK_SUCCESS;
}

int rme_readback_destroy(VkSwapchainKHR handle) {
    pthread_mutex_lock(&chains_lock);
    struct Chain **link = &chains;
    while (*link && (VkSwapchainKHR)(uintptr_t)*link != handle) link = &(*link)->next;
    struct Chain *chain = *link;
    if (chain) *link = chain->next;
    bool reset_interval = chain && chain->mailbox;
    for (struct Chain *other = chains; reset_interval && other; other = other->next)
        if (other->window == chain->window && !other->retired) reset_interval = false;
    pthread_mutex_unlock(&chains_lock);
    if (!chain) return 0;
    if (reset_interval) chain->window->setSwapInterval(chain->window, 1);
    free_chain(chain);
    return 1;
}

int rme_readback_images(VkSwapchainKHR handle, uint32_t *count, VkImage *out, VkResult *result) {
    struct Chain *chain = lookup_chain(handle);
    if (!chain) return 0;
    uint32_t written = out && *count < chain->count ? *count : chain->count;
    for (uint32_t i = 0; out && i < written; ++i) out[i] = chain->slots[i].image;
    *count = written;
    *result = written < chain->count ? VK_INCOMPLETE : VK_SUCCESS;
    return 1;
}

int rme_readback_acquire(VkSwapchainKHR handle, uint64_t timeout, VkSemaphore semaphore, VkFence fence,
    uint32_t *index, VkResult *result) {
    struct Chain *chain = lookup_chain(handle);
    if (!chain) return 0;
    *result = VK_ERROR_OUT_OF_DATE_KHR;
    if (chain->retired) return 1;
    for (uint32_t n = 0; n < chain->count; ++n) {
        uint32_t i = (chain->next_slot + n) % chain->count;
        if (chain->slots[i].acquired) continue;
        VkSubmitInfo signal = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .signalSemaphoreCount = semaphore != VK_NULL_HANDLE, .pSignalSemaphores = &semaphore};
        *result = vkQueueSubmit(chain->acquire_queue, 1, &signal, fence);
        if (*result == VK_SUCCESS) {
            chain->slots[i].acquired = true;
            chain->next_slot = (i + 1) % chain->count;
            *index = i;
        }
        return 1;
    }
    *result = timeout ? VK_TIMEOUT : VK_NOT_READY;
    return 1;
}

static VkResult setup_commands(struct Chain *chain, uint32_t family) {
    if (chain->pool && chain->family == family) return VK_SUCCESS;
    if (chain->pool) {
        vkDestroyCommandPool(chain->device, chain->pool, NULL);
        chain->pool = VK_NULL_HANDLE;
        for (uint32_t i = 0; i < chain->count; ++i) chain->slots[i].readback = VK_NULL_HANDLE;
    }
    if (!chain->complete) {
        VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkResult result = vkCreateFence(chain->device, &fence, NULL, &chain->complete);
        if (result != VK_SUCCESS) return result;
    }
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family};
    VkResult result = vkCreateCommandPool(chain->device, &pool, NULL, &chain->pool);
    if (result == VK_SUCCESS) chain->family = family;
    return result;
}

static VkResult record_readback(struct Chain *chain, struct ImageSlot *slot) {
    VkCommandBufferAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = chain->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    VkResult result = vkAllocateCommandBuffers(chain->device, &alloc, &command);
    if (result != VK_SUCCESS) return result;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    result = vkBeginCommandBuffer(command, &begin);
    if (result != VK_SUCCESS) goto free_command;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = slot->image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {chain->extent.width, chain->extent.height, 1}};
    vkCmdCopyImageToBuffer(command, slot->image, barrier.newLayout, chain->staging, 1, &copy);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = 0;
    VkMemoryBarrier cpu = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &cpu, 0, NULL, 1, &barrier);
    result = vkEndCommandBuffer(command);
    if (result == VK_SUCCESS) {
        slot->readback = command;
        return VK_SUCCESS;
    }

free_command:
    vkFreeCommandBuffers(chain->device, chain->pool, 1, &command);
    return result;
}

static VkResult download(struct Chain *chain, VkQueue queue, struct ImageSlot *slot, const VkPresentInfoKHR *info) {
    VK_FROM_HANDLE(vk_queue, vkq, queue);
    VkResult result = setup_commands(chain, vkq->queue_family_index);
    if (result != VK_SUCCESS) return result;
    if (!slot->readback) {
        result = record_readback(chain, slot);
        if (result != VK_SUCCESS) return result;
    }
    result = vkResetFences(chain->device, 1, &chain->complete);
    if (result != VK_SUCCESS) return result;
    VkPipelineStageFlags *stages = NULL;
    if (info->waitSemaphoreCount) {
        stages = calloc(info->waitSemaphoreCount, sizeof(*stages));
        if (!stages) return VK_ERROR_OUT_OF_HOST_MEMORY;
        for (uint32_t i = 0; i < info->waitSemaphoreCount; ++i) stages[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = info->waitSemaphoreCount,
        .pWaitSemaphores = info->pWaitSemaphores, .pWaitDstStageMask = stages,
        .commandBufferCount = 1, .pCommandBuffers = &slot->readback};
    result = vkQueueSubmit(queue, 1, &submit, chain->complete);
    free(stages);
    if (result != VK_SUCCESS) return result;
    result = vkWaitForFences(chain->device, 1, &chain->complete, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) return result;
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = chain->staging_memory, .size = VK_WHOLE_SIZE};
    return vkInvalidateMappedMemoryRanges(chain->device, 1, &range);
}

static void copy_bgra_row(uint8_t *dest, const uint8_t *src, uint32_t width) {
    const uint8x16_t rgba = {2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15};
    for (; width >= 4; width -= 4, dest += 16, src += 16) {
        const uint8x16_t bgra = vld1q_u8(src);
        vst1q_u8(dest, vqtbl1q_u8(bgra, rgba));
    }
    for (; width; --width, dest += 4, src += 4) {
        dest[0] = src[2];
        dest[1] = src[1];
        dest[2] = src[0];
        dest[3] = src[3];
    }
}

static VkResult post_pixels(struct Chain *chain) {
    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(chain->window, &buffer, NULL) != 0) return VK_ERROR_SURFACE_LOST_KHR;
    bool valid = buffer.bits && buffer.format == WINDOW_FORMAT_RGBA_8888 && buffer.stride >= buffer.width &&
        buffer.width == (int32_t)chain->extent.width && buffer.height == (int32_t)chain->extent.height;
    if (valid) {
        const bool bgra = chain->format == VK_FORMAT_B8G8R8A8_UNORM || chain->format == VK_FORMAT_B8G8R8A8_SRGB;
        for (uint32_t y = 0; y < chain->extent.height; ++y) {
            uint8_t *dest = (uint8_t *)buffer.bits + (size_t)y * buffer.stride * 4;
            const uint8_t *src = (const uint8_t *)chain->pixels + (size_t)y * chain->extent.width * 4;
            if (bgra) copy_bgra_row(dest, src, chain->extent.width);
            else memcpy(dest, src, (size_t)chain->extent.width * 4);
        }
    }
    if (ANativeWindow_unlockAndPost(chain->window) != 0) return VK_ERROR_SURFACE_LOST_KHR;
    return valid ? VK_SUCCESS : VK_ERROR_OUT_OF_DATE_KHR;
}

int rme_readback_present(VkQueue queue, const VkPresentInfoKHR *info, VkResult *result) {
    struct Chain *chain = NULL;
    for (uint32_t i = 0; !chain && i < info->swapchainCount; ++i) chain = lookup_chain(info->pSwapchains[i]);
    if (!chain) return 0;
    *result = VK_ERROR_OUT_OF_DATE_KHR;
    if (info->swapchainCount == 1 && !info->pNext && !chain->retired) {
        uint32_t i = info->pImageIndices[0];
        if (i < chain->count && chain->slots[i].acquired) {
            *result = download(chain, queue, &chain->slots[i], info);
            if (*result == VK_SUCCESS) *result = post_pixels(chain);
            chain->slots[i].acquired = false;
            if (*result != VK_SUCCESS) chain->retired = true;
        }
    }
    for (uint32_t i = 0; info->pResults && i < info->swapchainCount; ++i) info->pResults[i] = *result;
    return 1;
}
