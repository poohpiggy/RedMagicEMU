#pragma once
#include <vulkan/vulkan.h>

namespace gfxstream::vk { class VkEncoder; }
class QemuPipeStream;

enum class ComputeReadback { Query, Buffer };

int checkGuestCompute(gfxstream::vk::VkEncoder& client, QemuPipeStream& stream,
                      VkPhysicalDevice physical, const VkPhysicalDeviceProperties& properties,
                      ComputeReadback readback, bool expectMismatch);
