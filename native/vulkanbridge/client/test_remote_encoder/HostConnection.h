#pragma once
#include "VkEncoder.h"
enum { kCapsetGfxStreamVulkan };
class HostConnection {
public:
    static HostConnection* getOrCreate(int);
    gfxstream::vk::VkEncoder* vkEncoder();
};
