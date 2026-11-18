#ifndef RME_VK_DEVICE_RANK_H
#define RME_VK_DEVICE_RANK_H
#include <vulkan/vulkan.h>
static inline int device_rank(VkPhysicalDeviceType type) {
    if (type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) return 3;
    if (type == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) return 2;
    return 1;
}
#endif
