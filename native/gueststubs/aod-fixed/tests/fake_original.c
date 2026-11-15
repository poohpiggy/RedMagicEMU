/* Synthetic JNI fixture. It is not the vendor implementation. */
#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

unsigned char isSoFileExist;
static int status;
static int status_threw, alpha_threw;
static jboolean result_null;
static jint point[2];
static pthread_mutex_t init_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t init_cond = PTHREAD_COND_INITIALIZER;
static int init_waiting, init_continue;

void rme_test_wait_for_init(void) {
    pthread_mutex_lock(&init_gate);
    while (!init_waiting) pthread_cond_wait(&init_cond, &init_gate);
    pthread_mutex_unlock(&init_gate);
}
void rme_test_continue_init(void) {
    pthread_mutex_lock(&init_gate);
    init_continue = 1;
    pthread_cond_broadcast(&init_cond);
    pthread_mutex_unlock(&init_gate);
}

__attribute__((constructor)) static void constructor_reenter(void) {
    if (!getenv("RME_TEST_CONSTRUCTOR_REENTER")) return;
    void *proxy = dlopen(getenv("RME_TEST_PROXY_PATH"), RTLD_NOW | RTLD_NOLOAD);
    if (!proxy) abort();
    JNIEnv *(*mock_env)(void) = dlsym(RTLD_DEFAULT, "test_mock_env");
    jint (*probe)(JNIEnv *, jclass) = dlsym(proxy, "Java_com_zte_saliency_SaliencyApi_saliencyInitStatus");
    if (!mock_env || !probe) abort();
    (void)probe(mock_env(), (jclass)1);
    dlclose(proxy);
}

JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapInit(
    JNIEnv *env, jclass cls, jstring a, jstring b, jstring c) {
    (void)env; (void)cls;
    if (!a || !b || !c) return;
    if (getenv("RME_TEST_WAIT_INIT") && a == (jstring)3) {
        pthread_mutex_lock(&init_gate);
        init_waiting = 1;
        pthread_cond_broadcast(&init_cond);
        while (!init_continue) pthread_cond_wait(&init_cond, &init_gate);
        pthread_mutex_unlock(&init_gate);
        result_null = JNI_TRUE;
    } else if (getenv("RME_TEST_WAIT_INIT")) result_null = JNI_FALSE;
    if (getenv("RME_TEST_REENTER")) {
        void *proxy = dlopen(getenv("RME_TEST_PROXY_PATH"), RTLD_NOW | RTLD_NOLOAD);
        if (!proxy) abort();
        jint (*probe)(JNIEnv *, jclass) = dlsym(proxy, "Java_com_zte_saliency_SaliencyApi_saliencyInitStatus");
        if (!probe) abort();
        (void)probe(env, cls);
        dlclose(proxy);
    }
    if (a == (jstring)2) { (*env)->ThrowNew(env, (jclass)1, "synthetic init"); return; }
    isSoFileExist = getenv("RME_TEST_SO_MISSING") ? 0 : 1;
    status = getenv("RME_TEST_ORIGINAL_OK") ? 1 : -1;
}
JNIEXPORT jint JNICALL Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(JNIEnv *env, jclass cls) {
    (void)cls;
    if (status == -1 && getenv("RME_TEST_STATUS_THROWS") && !status_threw++)
        (*env)->ThrowNew(env, (jclass)1, "synthetic status");
    return status;
}
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(
    JNIEnv *env, jclass cls, jbyteArray input, jint width, jint height, jbyteArray output) {
    (void)cls;
    if ((*env)->ExceptionCheck(env)) return;
    if (status != 1) { result_null = JNI_TRUE; return; }
    if (!input || !output || width != 1 || height != 1) { result_null = JNI_TRUE; return; }
    jbyte rgb[3];
    (*env)->GetByteArrayRegion(env, input, 0, 3, rgb);
    if ((*env)->ExceptionCheck(env)) return;
    if ((unsigned char)rgb[0] == 0xee) { (*env)->ThrowNew(env, (jclass)1, "synthetic"); return; }
    const jbyte bgra[4] = {rgb[2], rgb[1], rgb[0], (jbyte)255};
    (*env)->SetByteArrayRegion(env, output, 0, 4, bgra);
    if (!(*env)->ExceptionCheck(env)) { result_null = JNI_FALSE; point[0] = 7; point[1] = 8; }
}
JNIEXPORT jboolean JNICALL Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(JNIEnv *env, jclass cls) {
    (void)cls;
    if (getenv("RME_TEST_ALPHA_THROWS") && !alpha_threw++)
        (*env)->ThrowNew(env, (jclass)1, "synthetic alpha");
    return result_null;
}
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(
    JNIEnv *env, jclass cls, jintArray out) {
    (void)cls;
    if (out && !(*env)->ExceptionCheck(env)) (*env)->SetIntArrayRegion(env, out, 0, 2, point);
}
#ifndef RME_TEST_OMIT_RELEASE
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRelease(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    if (isSoFileExist) { status = 0; isSoFileExist = 0; result_null = JNI_FALSE; }
}
#endif
