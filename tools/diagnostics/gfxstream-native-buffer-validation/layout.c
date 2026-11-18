#include <stddef.h>
#include <stdint.h>
_Static_assert(sizeof(void*) == 8, "this check is for Android ARM64");
_Static_assert(offsetof(VkNativeBufferANDROID, sType) == 0, "sType offset");
_Static_assert(offsetof(VkNativeBufferANDROID, pNext) == 8, "pNext offset");
_Static_assert(offsetof(VkNativeBufferANDROID, handle) == 16, "handle offset");
_Static_assert(offsetof(VkNativeBufferANDROID, stride) == 24, "stride offset");
_Static_assert(offsetof(VkNativeBufferANDROID, format) == 28, "format offset");
_Static_assert(offsetof(VkNativeBufferANDROID, usage) == 32, "usage offset");
_Static_assert(offsetof(VkNativeBufferANDROID, usage2) == 40, "usage2 offset");
_Static_assert(offsetof(VkNativeBufferANDROID, usage3) == 56, "usage3 offset");
#ifdef RME_AOSP_SPEC11
_Static_assert(VK_ANDROID_NATIVE_BUFFER_SPEC_VERSION == 11, "expected AOSP16 spec 11");
_Static_assert(offsetof(VkNativeBufferANDROID, ahb) == 64, "ahb offset");
_Static_assert(sizeof(VkNativeBufferANDROID) == 72, "AOSP16 struct size");
#else
_Static_assert(VK_ANDROID_NATIVE_BUFFER_SPEC_VERSION == 9, "expected d7a spec 9 header");
_Static_assert(sizeof(VkNativeBufferANDROID) == 64, "d7a struct size");
#endif
