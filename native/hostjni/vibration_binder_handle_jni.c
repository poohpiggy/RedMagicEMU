#include "vibration_binder_handle.h"
#include <jni.h>
#include <stdint.h>

/* Unwired experiment: these tokens are registry IDs, never native pointers.
 * The caller must provide descriptors from its managed Binder process and
 * serialize physical vibration with HostVibrationControl.stop. */
JNIEXPORT jlong JNICALL Java_app_aemu_core_VibrationBinderExperimentalNative_nativeOpen(
        JNIEnv *env, jclass cls, jint socket_fd, jint pidfd, jint pid, jint uid, jbyteArray input) {
    (void)cls;
    if (!input || (*env)->GetArrayLength(env,input) != 12 || pid <= 0 || uid < 0) return 0;
    unsigned char header[12];
    (*env)->GetByteArrayRegion(env,input,0,12,(jbyte *)header);
    if ((*env)->ExceptionCheck(env)) return 0;
    return (jlong)rme_vib_handle_open(socket_fd,pidfd,(pid_t)pid,(uid_t)uid,header);
}
JNIEXPORT jlongArray JNICALL Java_app_aemu_core_VibrationBinderExperimentalNative_nativeNext(
        JNIEnv *env, jclass cls, jlong id) {
    (void)cls;
    struct rme_vib_step_event e = {0};
    int status = rme_vib_handle_next((uint64_t)id,&e);
    jlong values[4] = {(jlong)status,(jlong)e.token,(jlong)e.code,(jlong)e.duration_ms};
    jlongArray result=(*env)->NewLongArray(env,4);
    if (result) (*env)->SetLongArrayRegion(env,result,0,4,values);
    return result;
}
JNIEXPORT jint JNICALL Java_app_aemu_core_VibrationBinderExperimentalNative_nativeComplete(
        JNIEnv *env, jclass cls, jlong id, jlong token, jboolean success) {
    (void)env; (void)cls;
    return rme_vib_handle_complete((uint64_t)id,(uint64_t)token,success == JNI_TRUE);
}
JNIEXPORT jint JNICALL Java_app_aemu_core_VibrationBinderExperimentalNative_nativeStop(
        JNIEnv *env, jclass cls, jlong id) {
    (void)env; (void)cls; return rme_vib_handle_stop((uint64_t)id);
}
JNIEXPORT jint JNICALL Java_app_aemu_core_VibrationBinderExperimentalNative_nativeDestroy(
        JNIEnv *env, jclass cls, jlong id) {
    (void)env; (void)cls; return rme_vib_handle_destroy((uint64_t)id);
}
