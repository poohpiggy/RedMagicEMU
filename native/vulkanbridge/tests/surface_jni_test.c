#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "../surface_jni.h"

struct ANativeWindow { int refs; };
static struct ANativeWindow first, second;
static int conversions;
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface) {
    (void)env; ++conversions;
    if (surface == (jobject)(uintptr_t)1) { ++first.refs; return &first; }
    if (surface == (jobject)(uintptr_t)2) { ++second.refs; return &second; }
    return NULL;
}
void ANativeWindow_acquire(ANativeWindow* window) { ++window->refs; }
void ANativeWindow_release(ANativeWindow* window) { assert(window->refs > 0); --window->refs; }
int main(void) {
    JNIEnv* env = (JNIEnv*)(uintptr_t)1;
    assert(!rme_vk_jni_acquire_window());
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env, NULL, (jobject)(uintptr_t)1));
    assert(first.refs == 1);
    RmeVkHostWindow* lease = rme_vk_jni_acquire_window();
    assert(lease && first.refs == 2);
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env, NULL, (jobject)(uintptr_t)2));
    assert(first.refs == 1 && second.refs == 1);
    rme_vk_host_window_release(lease);
    assert(first.refs == 0);
    assert(!Java_app_aemu_core_VulkanSurface_nativeUpdate(env, NULL, (jobject)(uintptr_t)3));
    assert(second.refs == 0 && !rme_vk_jni_acquire_window());
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env, NULL, (jobject)(uintptr_t)2));
    lease = rme_vk_jni_acquire_window();
    assert(lease && second.refs == 2);
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env, NULL, NULL));
    assert(second.refs == 1 && !rme_vk_jni_acquire_window() && conversions == 4);
    rme_vk_host_window_release(lease);
    assert(second.refs == 0);
    puts("JNI_SURFACE_WINDOW_OWNERSHIP_OK");
}
