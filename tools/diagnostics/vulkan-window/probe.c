#define VK_USE_PLATFORM_ANDROID_KHR
#include <android/log.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define TAG "RmeVulkanWindow"
#define CHECK(call) do { \
    __android_log_print(ANDROID_LOG_INFO, TAG, "Checking %s", #call); \
    VkResult result = (call); \
    if (result != VK_SUCCESS) { \
        __android_log_print(ANDROID_LOG_ERROR, TAG, "%s failed: %d", #call, result); \
        goto done; \
    } \
} while (0)

static uint32_t memory_type(VkPhysicalDevice physical, uint32_t mask, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    for (uint32_t n = 0; n < properties.memoryTypeCount; ++n) {
        if ((mask & (1u << n)) && (properties.memoryTypes[n].propertyFlags & flags) == flags) return n;
    }
    return UINT32_MAX;
}

static void draw(ANativeActivity *activity, ANativeWindow *window) {
    (void)activity;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR chain = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence acquired = VK_NULL_HANDLE;
    VkFence rendered = VK_NULL_HANDLE;
    VkSurfaceFormatKHR *formats = NULL;
    VkImage *images = NULL;
    VkQueueFamilyProperties *families = NULL;
    VkPhysicalDevice *physical_devices = NULL;
    VkQueue queue = VK_NULL_HANDLE;
    VkImage offscreen = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    const uint8_t *pixels = NULL;
    VkImage image;
    VkExtent2D extent;
    VkSurfaceFormatKHR format;
#ifdef RME_WINDOW_READBACK_CONTROL
    const int direct = 0;
#else
    const int direct = 1;
#endif
    const char *instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = TAG, .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app_info,
        .enabledExtensionCount = direct ? 2 : 0, .ppEnabledExtensionNames = direct ? instance_extensions : NULL,
    };
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    const VkAndroidSurfaceCreateInfoKHR surface_info = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = window,
    };
    if (direct) CHECK(vkCreateAndroidSurfaceKHR(instance, &surface_info, NULL, &surface));

    uint32_t physical_count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, NULL));
    if (!physical_count || !(physical_devices = calloc(physical_count, sizeof(*physical_devices)))) goto done;
    CHECK(vkEnumeratePhysicalDevices(instance, &physical_count, physical_devices));
    VkPhysicalDevice physical = physical_devices[0];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    __android_log_print(ANDROID_LOG_INFO, TAG, "Device: %s (type %u)", props.deviceName, props.deviceType);

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, NULL);
    if (!queue_count || !(families = calloc(queue_count, sizeof(*families)))) goto done;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, families);
    uint32_t family = 0;
    for (; family < queue_count; ++family) {
        VkBool32 support = VK_TRUE;
        if (direct) CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &support));
        if (support && families[family].queueCount && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) break;
    }
    if (family == queue_count) goto done;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family,
        .queueCount = 1, .pQueuePriorities = &priority,
    };
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = direct ? 1 : 0, .ppEnabledExtensionNames = direct ? device_extensions : NULL,
    };
    CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    vkGetDeviceQueue(device, family, 0, &queue);

    const VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CHECK(vkCreateFence(device, &fence_info, NULL, &rendered));
    const uint64_t timeout = 2000000000;
    uint32_t index = 0;
    if (direct) {
        VkSurfaceCapabilitiesKHR caps;
        CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Surface cannot receive a Vulkan clear");
            goto done;
        }
        uint32_t format_count = 0;
        CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, NULL));
        if (!format_count || !(formats = calloc(format_count, sizeof(*formats)))) goto done;
        CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats));
        format = formats[0];
        if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_R8G8B8A8_UNORM;
        extent = caps.currentExtent;
        if (extent.width == UINT32_MAX) {
            extent.width = (uint32_t)ANativeWindow_getWidth(window);
            extent.height = (uint32_t)ANativeWindow_getHeight(window);
        }
#ifdef RME_WINDOW_MAILBOX_CONTROL
        const VkPresentModeKHR present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        VkPresentModeKHR modes[8];
        uint32_t mode_count = sizeof(modes) / sizeof(modes[0]);
        CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &mode_count, modes));
        VkBool32 found = VK_FALSE;
        for (uint32_t i = 0; i < mode_count; ++i) found |= modes[i] == present_mode;
        if (!found) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "MAILBOX_NOT_ADVERTISED");
            goto done;
        }
#else
        const VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
#endif
        const VkSwapchainCreateInfoKHR swapchain_info = {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface,
            .minImageCount = caps.minImageCount, .imageFormat = format.format, .imageColorSpace = format.colorSpace,
            .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = caps.currentTransform,
            .compositeAlpha = caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha,
            .presentMode = present_mode, .clipped = VK_TRUE,
        };
        CHECK(vkCreateSwapchainKHR(device, &swapchain_info, NULL, &chain));
        uint32_t image_count = 0;
        CHECK(vkGetSwapchainImagesKHR(device, chain, &image_count, NULL));
        if (!image_count || !(images = calloc(image_count, sizeof(*images)))) goto done;
        CHECK(vkGetSwapchainImagesKHR(device, chain, &image_count, images));
        CHECK(vkCreateFence(device, &fence_info, NULL, &acquired));
        CHECK(vkAcquireNextImageKHR(device, chain, timeout, VK_NULL_HANDLE, acquired, &index));
        CHECK(vkWaitForFences(device, 1, &acquired, VK_TRUE, timeout));
        if (index >= image_count) goto done;
        image = images[index];
    } else {
        extent = (VkExtent2D){128, 128};
        format = (VkSurfaceFormatKHR){VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        const VkImageCreateInfo image_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format.format,
            .extent = {extent.width, extent.height, 1}, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        };
        CHECK(vkCreateImage(device, &image_info, NULL, &offscreen));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, offscreen, &req);
        VkMemoryAllocateInfo mem = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = req.size, .memoryTypeIndex = memory_type(physical, req.memoryTypeBits, 0)};
        if (mem.memoryTypeIndex == UINT32_MAX) goto done;
        CHECK(vkAllocateMemory(device, &mem, NULL, &image_memory));
        CHECK(vkBindImageMemory(device, offscreen, image_memory, 0));
        const VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = (VkDeviceSize)extent.width * extent.height * 4, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        CHECK(vkCreateBuffer(device, &buffer_info, NULL, &staging));
        vkGetBufferMemoryRequirements(device, staging, &req);
        mem.allocationSize = req.size;
        mem.memoryTypeIndex = memory_type(physical, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        if (mem.memoryTypeIndex == UINT32_MAX) goto done;
        CHECK(vkAllocateMemory(device, &mem, NULL, &staging_memory));
        CHECK(vkBindBufferMemory(device, staging, staging_memory, 0));
        void *mapped = NULL;
        CHECK(vkMapMemory(device, staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
        pixels = mapped;
        image = offscreen;
    }
    const VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family,
    };
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    const VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &alloc_info, &cmd));
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    const VkClearColorValue color = {.float32 = {0.125f, 0.75f, 0.5f, 1.0f}};
    vkCmdClearColorImage(cmd, image, barrier.newLayout, &color, 1, &barrier.subresourceRange);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = direct ? 0 : VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = direct ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        direct ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    if (!direct) {
        const VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageExtent = {extent.width, extent.height, 1}};
        vkCmdCopyImageToBuffer(cmd, image, barrier.newLayout, staging, 1, &region);
        const VkMemoryBarrier copy_complete = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &copy_complete, 0, NULL, 0, NULL);
    }
    CHECK(vkEndCommandBuffer(cmd));
    const VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    CHECK(vkQueueSubmit(queue, 1, &submit, rendered));
    CHECK(vkWaitForFences(device, 1, &rendered, VK_TRUE, timeout));
    const VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .swapchainCount = 1, .pSwapchains = &chain, .pImageIndices = &index,
    };
    if (direct) {
        CHECK(vkQueuePresentKHR(queue, &present));
        CHECK(vkQueueWaitIdle(queue));
        __android_log_print(ANDROID_LOG_INFO, TAG, "PRESENT_OK %ux%u format=%u", extent.width, extent.height, format.format);
    } else {
        const VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = staging_memory, .size = VK_WHOLE_SIZE};
        CHECK(vkInvalidateMappedMemoryRanges(device, 1, &range));
        for (size_t i = 0; i < (size_t)extent.width * extent.height * 4; i += 4) {
            if (abs(pixels[i] - 32) > 1 || abs(pixels[i+1] - 191) > 1 ||
                abs(pixels[i+2] - 128) > 1 || pixels[i+3] != 255) {
                __android_log_print(ANDROID_LOG_ERROR, TAG, "Readback mismatch at %zu: %u %u %u %u",
                    i / 4, pixels[i], pixels[i+1], pixels[i+2], pixels[i+3]);
                goto done;
            }
        }
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_setBuffersGeometry(window, extent.width, extent.height, WINDOW_FORMAT_RGBA_8888) ||
            ANativeWindow_lock(window, &buffer, NULL)) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not lock Android window for readback");
            goto done;
        }
        if (buffer.bits && buffer.width == (int32_t)extent.width && buffer.height == (int32_t)extent.height &&
            buffer.stride >= buffer.width && buffer.format == WINDOW_FORMAT_RGBA_8888) {
            for (size_t y = 0; y < extent.height; ++y)
                memcpy((uint8_t *)buffer.bits + y * buffer.stride * 4, pixels + y * extent.width * 4, extent.width * 4);
            if (ANativeWindow_unlockAndPost(window) == 0)
                __android_log_print(ANDROID_LOG_INFO, TAG, "READBACK_PRESENT_OK %ux%u", extent.width, extent.height);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Unexpected Android window buffer layout");
            ANativeWindow_unlockAndPost(window);
        }
    }

done:
    if (queue) vkQueueWaitIdle(queue);
    if (pool) vkDestroyCommandPool(device, pool, NULL);
    if (rendered) vkDestroyFence(device, rendered, NULL);
    if (acquired) vkDestroyFence(device, acquired, NULL);
    if (pixels) vkUnmapMemory(device, staging_memory);
    if (staging) vkDestroyBuffer(device, staging, NULL);
    if (offscreen) vkDestroyImage(device, offscreen, NULL);
    if (staging_memory) vkFreeMemory(device, staging_memory, NULL);
    if (image_memory) vkFreeMemory(device, image_memory, NULL);
    if (chain) vkDestroySwapchainKHR(device, chain, NULL);
    if (device) vkDestroyDevice(device, NULL);
    if (surface) vkDestroySurfaceKHR(instance, surface, NULL);
    if (instance) vkDestroyInstance(instance, NULL);
    free(images);
    free(formats);
    free(families);
    free(physical_devices);
}

void ANativeActivity_onCreate(ANativeActivity *activity, void *state, size_t state_size) {
    (void)state;
    (void)state_size;
    ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    activity->callbacks->onNativeWindowCreated = draw;
}
