#ifndef RME_VULKANBRIDGE_SURFACE_JNI_H
#define RME_VULKANBRIDGE_SURFACE_JNI_H
#include <jni.h>
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#include "host_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT jboolean JNICALL Java_app_aemu_core_VulkanSurface_nativeUpdate(JNIEnv*, jclass, jobject);

/* Host-local API; returns a separately owned reference or NULL. Never expose
 * this pointer to a Guest or another process. The surface consumer must be
 * stopped and its VkSurface/swapchain destroyed before Surface destruction.
 * That handoff is not yet implemented, so no product caller may use it. */
RmeVkHostWindow* rme_vk_jni_acquire_window(void);

#ifdef __cplusplus
}
#endif
#endif
