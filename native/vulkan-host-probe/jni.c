#include <jni.h>
#include <stddef.h>

int vulkan_host_probe(char *result, size_t size, int bad_expectation);

JNIEXPORT jstring JNICALL
Java_app_aemu_core_HostVulkanProbe_nativeRun(JNIEnv *env, jobject self) {
    (void)self;
    char result[8192];
    (void)vulkan_host_probe(result, sizeof(result), 0);
    /* Driver names are untrusted bytes; NewStringUTF requires valid modified UTF-8. */
    for (unsigned char *p = (unsigned char *)result; *p; ++p)
        if (*p >= 127 || (*p < 32 && *p != '\n' && *p != '\t')) *p = '?';
    return (*env)->NewStringUTF(env, result);
}
