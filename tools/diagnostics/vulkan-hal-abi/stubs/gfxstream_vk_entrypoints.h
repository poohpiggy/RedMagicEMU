/* Test-only declarations matching the generated gfxstream entrypoints. */
#include <vulkan/vulkan.h>
#ifdef __cplusplus
extern "C" {
#endif
VKAPI_ATTR VkResult VKAPI_CALL gfxstream_vk_EnumerateInstanceExtensionProperties(const char *, uint32_t *, VkExtensionProperties *);
VKAPI_ATTR VkResult VKAPI_CALL gfxstream_vk_CreateInstance(const VkInstanceCreateInfo *, const VkAllocationCallbacks *, VkInstance *);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL gfxstream_vk_GetInstanceProcAddr(VkInstance, const char *);
#ifdef __cplusplus
}
#endif
