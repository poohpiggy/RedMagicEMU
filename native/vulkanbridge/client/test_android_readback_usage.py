#!/usr/bin/env python3
"""Check readback swapchain supported usage flags and creation validation."""
import argparse
from pathlib import Path
import subprocess
import tempfile


SOURCE = Path(__file__).with_name("android_readback_wsi.c")


def production_slice():
    source = SOURCE.read_text(encoding="utf-8")
    return source[source.index("enum { MIN_IMAGES"):source.index("int rme_readback_destroy")]


TEST = r"""
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

enum { WINDOW_FORMAT_RGBA_8888 = 1 };

struct ANativeWindow {
    int width;
    int height;
    int minSwapInterval;
    int (*setSwapInterval)(struct ANativeWindow *, int);
};

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct vk_queue {
    struct list_head link;
    uint32_t queue_family_index;
};

struct vk_device {
    void *physical;
    struct list_head queues;
    VkAllocationCallbacks alloc;
};

#define VK_FROM_HANDLE(type, name, handle) struct type *name = (struct type *)(handle)
#define vk_physical_device_to_handle(p) ((VkPhysicalDevice)(p))
#define vk_queue_to_handle(q) ((VkQueue)(q))
#define list_is_empty(h) ((h)->next == (h))
#define list_first_entry(h, type, member) ((type *)((char *)((h)->next) - __builtin_offsetof(type, member)))
#define HANDLE(type, number) ((type)(uintptr_t)(number))

static struct ANativeWindow test_window;
static VkImageUsageFlags last_created_usage;
static uint32_t images_created;

static void *test_alloc(void *user, size_t size, size_t align, VkSystemAllocationScope scope) {
    (void)user;
    (void)align;
    (void)scope;
    return malloc(size);
}

static void test_free(void *user, void *ptr) {
    (void)user;
    free(ptr);
}

static int set_swap_interval(struct ANativeWindow *w, int interval) {
    (void)w;
    (void)interval;
    return 0;
}

static struct ANativeWindow *rme_android_surface_window_vk(VkSurfaceKHR surface) {
    return surface == HANDLE(VkSurfaceKHR, 1) ? &test_window : NULL;
}

static int ANativeWindow_getWidth(struct ANativeWindow *w) { return w->width; }
static int ANativeWindow_getHeight(struct ANativeWindow *w) { return w->height; }
static void ANativeWindow_acquire(struct ANativeWindow *w) { (void)w; }
static void ANativeWindow_release(struct ANativeWindow *w) { (void)w; }
static int ANativeWindow_setBuffersGeometry(struct ANativeWindow *w, int width, int height, int format) {
    (void)w;
    (void)width;
    (void)height;
    return format == WINDOW_FORMAT_RGBA_8888 ? 0 : -1;
}

void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p, uint32_t *count, VkQueueFamilyProperties *props) {
    (void)p;
    if (!props) {
        *count = 1;
        return;
    }
    props[0] = (VkQueueFamilyProperties){.queueFlags = VK_QUEUE_GRAPHICS_BIT, .queueCount = 1};
}

void vkGetPhysicalDeviceProperties(VkPhysicalDevice p, VkPhysicalDeviceProperties *props) {
    (void)p;
    *props = (VkPhysicalDeviceProperties){.limits = {.maxImageDimension2D = 16384}};
}

VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice p, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties *props) {
    (void)p;
    (void)format;
    (void)type;
    (void)tiling;
    (void)flags;
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    *props = (VkImageFormatProperties){.sampleCounts = VK_SAMPLE_COUNT_1_BIT};
    return VK_SUCCESS;
}

void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties *props) {
    (void)p;
    *props = (VkPhysicalDeviceMemoryProperties){
        .memoryTypeCount = 2,
        .memoryTypes = {
            {.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT},
            {.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT},
        },
    };
}

VkResult vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *a, VkDeviceMemory *out) {
    (void)d; (void)info; (void)a;
    *out = HANDLE(VkDeviceMemory, 10);
    return VK_SUCCESS;
}

void vkFreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; }
void vkDestroyCommandPool(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; }
void vkDestroyFence(VkDevice d, VkFence f, const VkAllocationCallbacks *a) { (void)d; (void)f; (void)a; }
void vkUnmapMemory(VkDevice d, VkDeviceMemory m) { (void)d; (void)m; }
void vkDestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) { (void)d; (void)b; (void)a; }
void vkDestroyImage(VkDevice d, VkImage i, const VkAllocationCallbacks *a) { (void)d; (void)i; (void)a; }

VkResult vkCreateImage(VkDevice d, const VkImageCreateInfo *info, const VkAllocationCallbacks *a, VkImage *out) {
    (void)d; (void)a;
    last_created_usage = info->usage;
    *out = HANDLE(VkImage, ++images_created);
    return VK_SUCCESS;
}

void vkGetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *req) {
    (void)d; (void)i;
    *req = (VkMemoryRequirements){.size = 4096, .alignment = 16, .memoryTypeBits = 1};
}

VkResult vkBindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, VkDeviceSize o) {
    (void)d; (void)i; (void)m; (void)o;
    return VK_SUCCESS;
}

VkResult vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *info, const VkAllocationCallbacks *a, VkBuffer *out) {
    (void)d; (void)info; (void)a;
    *out = HANDLE(VkBuffer, 20);
    return VK_SUCCESS;
}

void vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *req) {
    (void)d; (void)b;
    *req = (VkMemoryRequirements){.size = 4096, .alignment = 16, .memoryTypeBits = 2};
}

VkResult vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize o) {
    (void)d; (void)b; (void)m; (void)o;
    return VK_SUCCESS;
}

VkResult vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **data) {
    (void)d; (void)m; (void)o; (void)s; (void)f;
    static uint8_t pixels[16];
    *data = pixels;
    return VK_SUCCESS;
}

/* production */

int main(void) {
    test_window = (struct ANativeWindow){
        .width = 912, .height = 2016, .minSwapInterval = 0, .setSwapInterval = set_swap_interval,
    };
    struct vk_queue queue = {0};
    struct vk_device dev = {
        .physical = (void *)0x1234,
        .alloc = {.pfnAllocation = test_alloc, .pfnFree = test_free},
    };
    dev.queues.next = &queue.link;
    dev.queues.prev = &queue.link;
    queue.link.next = &dev.queues;
    queue.link.prev = &dev.queues;

    VkSurfaceCapabilitiesKHR caps = {0};
    assert(rme_readback_capabilities((VkPhysicalDevice)dev.physical, HANDLE(VkSurfaceKHR, 1), &caps) == VK_SUCCESS);
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
        fprintf(stderr, "FAIL: supportedUsageFlags=0x%x missing VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT\n",
            caps.supportedUsageFlags);
        return EXIT_FAILURE;
    }

    const VkImageUsageFlags unity_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = HANDLE(VkSurfaceKHR, 1),
        .minImageCount = 3,
        .imageFormat = VK_FORMAT_R8G8B8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {912, 2016},
        .imageArrayLayers = 1,
        .imageUsage = unity_usage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkResult res = rme_readback_create((VkDevice)&dev, &info, NULL, &swapchain);
    if (res != VK_SUCCESS || swapchain == VK_NULL_HANDLE) {
        fprintf(stderr, "FAIL: rme_readback_create(usage=0x%x) returned %d\n", unity_usage, res);
        return EXIT_FAILURE;
    }
    assert(images_created == 3);
    assert(last_created_usage == unity_usage);

    info.imageUsage = unity_usage | VK_IMAGE_USAGE_STORAGE_BIT;
    VkSwapchainKHR rejected = VK_NULL_HANDLE;
    assert(rme_readback_create((VkDevice)&dev, &info, NULL, &rejected) == VK_ERROR_FEATURE_NOT_PRESENT);
    assert(rejected == VK_NULL_HANDLE);

    puts("PASS: readback swapchain advertises and accepts VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT");
    return EXIT_SUCCESS;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vulkan_include", type=Path, help="Directory containing vulkan/vulkan_core.h")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="rme-vk-readback-usage-") as work:
        source = Path(work) / "test.c"
        source.write_text(TEST.replace("/* production */", production_slice()), encoding="utf-8")
        executable = Path(work) / "test"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(args.vulkan_include), source, "-o", executable], check=True)
        subprocess.run([executable], check=True)


if __name__ == "__main__":
    main()
