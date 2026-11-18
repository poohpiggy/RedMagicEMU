#include <jni.h>
#include <stddef.h>
#include <stdio.h>

int opencl_host_probe(char *result, size_t size);
int opencl_discovery_roundtrip(char *result, size_t size);

JNIEXPORT jstring JNICALL
Java_app_aemu_core_HostOpenClProbe_nativeRun(JNIEnv *env, jobject self) {
    char compute[512], discovery[512], result[1024];
    (void)self;
    (void)opencl_host_probe(compute, sizeof(compute));
    (void)opencl_discovery_roundtrip(discovery, sizeof(discovery));
    snprintf(result, sizeof(result), "%s\n%s", compute, discovery);
    return (*env)->NewStringUTF(env, result);
}
