#include "gpu_select.h"
#include <string.h>

enum { MAX_DEVICES = 256, MAX_QUEUE_FAMILIES = 256, MAX_ATTEMPTS = 3 };

enum rme_vk_select_status rme_vk_select_gpu(VkInstance instance,
    PFN_vkGetInstanceProcAddr get_proc, struct rme_vk_gpu_selection *out) {
    if (!out) return RME_VK_SELECT_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->status = RME_VK_SELECT_INVALID_ARGUMENT;
    if (!instance || !get_proc) return out->status;
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)get_proc(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties properties = (PFN_vkGetPhysicalDeviceProperties)get_proc(instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceQueueFamilyProperties queues = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)get_proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    if (!enumerate || !properties || !queues) return out->status = RME_VK_SELECT_FUNCTION_UNAVAILABLE;
    VkPhysicalDevice devices[MAX_DEVICES];
    uint32_t count = 0;
    for (unsigned attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        count = 0;
        VkResult r = enumerate(instance, &count, NULL);
        out->vk_result = r;
        if (r != VK_SUCCESS) return out->status = RME_VK_SELECT_ENUMERATION_FAILED;
        if (!count) return out->status = RME_VK_SELECT_NO_SUITABLE_DEVICE;
        if (count > MAX_DEVICES) return out->status = RME_VK_SELECT_LIMIT_EXCEEDED;
        uint32_t capacity = count;
        r = enumerate(instance, &count, devices);
        out->vk_result = r;
        if (r == VK_INCOMPLETE) {
            if (attempt + 1 == MAX_ATTEMPTS) return out->status = RME_VK_SELECT_ENUMERATION_FAILED;
            continue;
        }
        if (r != VK_SUCCESS) return out->status = RME_VK_SELECT_ENUMERATION_FAILED;
        if (count > capacity) return out->status = RME_VK_SELECT_INVALID_RESPONSE;
        break;
    }
    for (uint32_t i=0; i<count; ++i) {
        if (!devices[i]) return out->status = RME_VK_SELECT_INVALID_RESPONSE;
        VkPhysicalDeviceProperties p;
        memset(&p, 0, sizeof(p));
        properties(devices[i], &p); /* Vulkan returns no VkResult for this query. */
        if ((p.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
             p.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
            VK_API_VERSION_VARIANT(p.apiVersion) != 0 || p.apiVersion < VK_API_VERSION_1_2) continue;
        uint32_t n = 0;
        queues(devices[i], &n, NULL); /* This query also returns no VkResult. */
        if (!n) continue;
        if (n > MAX_QUEUE_FAMILIES) return out->status = RME_VK_SELECT_LIMIT_EXCEEDED;
        VkQueueFamilyProperties q[MAX_QUEUE_FAMILIES];
        memset(q, 0, sizeof(q));
        uint32_t capacity = n;
        queues(devices[i], &n, q);
        uint32_t current_count = 0;
        queues(devices[i], &current_count, NULL);
        if (n != capacity || current_count != n)
            return out->status = RME_VK_SELECT_INVALID_RESPONSE;
        uint32_t family = n;
        for (uint32_t j=0; j<n; ++j) {
            if (!q[j].queueCount || !(q[j].queueFlags & VK_QUEUE_COMPUTE_BIT)) continue;
            if (family == n) family = j;
            if (q[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = j; break; }
        }
        if (family < n) {
            out->device = devices[i]; out->queue_family = family; out->properties = p;
            return out->status = RME_VK_SELECT_OK;
        }
    }
    return out->status = RME_VK_SELECT_NO_SUITABLE_DEVICE;
}
