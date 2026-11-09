#include "vibration_publish.h"
#include <errno.h>
#include <jni.h>

JNIEXPORT jint JNICALL Java_app_aemu_core_VibrationBinderExperimentalNative_nativePublishNoReplace(
        JNIEnv *env, jclass cls, jint dir, jstring stage, jstring target) {
    (void)cls;
    if (!stage || !target) return -EINVAL;
    const char *a=(*env)->GetStringUTFChars(env,stage,0);
    if (!a) return -ENOMEM;
    const char *b=(*env)->GetStringUTFChars(env,target,0);
    if (!b) { (*env)->ReleaseStringUTFChars(env,stage,a); return -ENOMEM; }
    int rc=rme_vib_publish_noreplace(dir,a,b);
    (*env)->ReleaseStringUTFChars(env,target,b);
    (*env)->ReleaseStringUTFChars(env,stage,a);
    return rc;
}
