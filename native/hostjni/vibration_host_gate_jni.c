#include "vibration_host_gate.h"
#include <jni.h>

JNIEXPORT jint JNICALL Java_app_aemu_core_HostNative_vibrationRead(
        JNIEnv *env, jclass cls, jint socket_fd, jint pidfd, jint pid, jint uid,
        jlong epoch, jlong sequence) {
    (void)env; (void)cls;
    if (uid < 0 || epoch <= 0 || sequence <= 0) return -1;
    return rme_host_read_vibration(socket_fd, pidfd, pid, (uint32_t)uid,
                                   (uint64_t)epoch, (uint64_t)sequence);
}
