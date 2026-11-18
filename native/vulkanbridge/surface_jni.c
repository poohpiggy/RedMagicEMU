#include "surface_jni.h"
#include <android/native_window_jni.h>
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow* current;

JNIEXPORT jboolean JNICALL Java_app_aemu_core_VulkanSurface_nativeUpdate(JNIEnv* env, jclass cls, jobject surface) {
    (void)cls;
    if (!env) return JNI_FALSE;
    pthread_mutex_lock(&lock);
    ANativeWindow* next = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    ANativeWindow* previous = current;
    // A failed conversion invalidates the previous surface too: it might
    // already have been destroyed by the UI lifecycle.
    current = next;
    if (previous) ANativeWindow_release(previous);
    pthread_mutex_unlock(&lock);
    return !surface || next ? JNI_TRUE : JNI_FALSE;
}

RmeVkHostWindow* rme_vk_jni_acquire_window(void) {
    pthread_mutex_lock(&lock);
    RmeVkHostWindow* result = current ? rme_vk_host_window_acquire(current) : NULL;
    pthread_mutex_unlock(&lock);
    return result;
}
