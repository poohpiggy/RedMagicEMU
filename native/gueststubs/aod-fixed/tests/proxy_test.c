/* JNI is mocked; these tests do not execute Android ART or vendor libraries. */
#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define P "Java_com_zte_saliency_SaliencyApi_"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
struct array { int n; unsigned char b[4]; jint xy[2]; };
static int exception, fail_write;
static void (*release_during_read)(JNIEnv *, jclass);
static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_cond = PTHREAD_COND_INITIALIZER;
static int block_read, read_entered, allow_read;
static jboolean pending(JNIEnv *e) { (void)e; return exception ? JNI_TRUE : JNI_FALSE; }
static jint throwing(JNIEnv *e, jclass c, const char *s) { (void)e; (void)c; (void)s; exception = 1; return 0; }
static jclass find_class(JNIEnv *e, const char *name) { (void)e; (void)name; return (jclass)1; }
static void delete_ref(JNIEnv *e, jobject ref) { (void)e; (void)ref; }
static jsize length(JNIEnv *e, jarray a) { (void)e; return ((struct array *)a)->n; }
static void get_bytes(JNIEnv *e, jbyteArray a, jsize off, jsize n, jbyte *b) {
    if (release_during_read) {
        void (*release)(JNIEnv *, jclass) = release_during_read;
        release_during_read = NULL;
        release(e, (jclass)1);
    }
    pthread_mutex_lock(&test_lock);
    if (block_read) {
        read_entered = 1;
        pthread_cond_broadcast(&test_cond);
        while (!allow_read) pthread_cond_wait(&test_cond, &test_lock);
    }
    pthread_mutex_unlock(&test_lock);
    memcpy(b, ((struct array *)a)->b + off, (size_t)n);
}
static void set_bytes(JNIEnv *e, jbyteArray a, jsize off, jsize n, const jbyte *b) {
    (void)e; if (fail_write) { exception = 1; return; }
    memcpy(((struct array *)a)->b + off, b, (size_t)n);
}
static void set_ints(JNIEnv *e, jintArray a, jsize off, jsize n, const jint *b) {
    (void)e; memcpy(((struct array *)a)->xy + off, b, (size_t)n * sizeof(jint));
}
static jboolean same(JNIEnv *e, jobject a, jobject b) { (void)e; return a == b; }
static const struct JNINativeInterface vt = {
    .ExceptionCheck = pending, .ThrowNew = throwing, .FindClass = find_class,
    .DeleteLocalRef = delete_ref, .GetArrayLength = length,
    .GetByteArrayRegion = get_bytes, .SetByteArrayRegion = set_bytes,
    .SetIntArrayRegion = set_ints, .IsSameObject = same,
};
static JNIEnv test_env = &vt;
JNIEnv *test_mock_env(void) { return &test_env; }

struct run_task {
    void (*run)(JNIEnv *, jclass, jbyteArray, jint, jint, jbyteArray);
    JNIEnv *env;
    struct array *in, *out;
};
struct init_task {
    void (*init)(JNIEnv *, jclass, jstring, jstring, jstring);
    JNIEnv *env;
};
static void *init_in_thread(void *arg) {
    struct init_task *task = arg;
    task->init(task->env, (jclass)1, (jstring)3, (jstring)1, (jstring)1);
    return NULL;
}
static void *run_in_thread(void *arg) {
    struct run_task *task = arg;
    task->run(task->env, (jclass)1, (jbyteArray)task->in, 1, 1, (jbyteArray)task->out);
    return NULL;
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    JNIEnv *e = test_mock_env(); jclass c = (jclass)1;
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); CHECK(h);
    void (*init)(JNIEnv *, jclass, jstring, jstring, jstring) = dlsym(h, P "saliencyMapInit");
    jint (*status)(JNIEnv *, jclass) = dlsym(h, P "saliencyInitStatus");
    void (*run)(JNIEnv *, jclass, jbyteArray, jint, jint, jbyteArray) = dlsym(h, P "saliencyMapRunning");
    jboolean (*alpha)(JNIEnv *, jclass) = dlsym(h, P "saliencyIsAlphaNull");
    void (*highest)(JNIEnv *, jclass, jintArray) = dlsym(h, P "saliencyMapHighestPointGet");
    void (*release)(JNIEnv *, jclass) = dlsym(h, P "saliencyMapRelease");
    CHECK(init && status && run && alpha && highest && release);
    struct array in = {.n=3, .b={1,2,3}}, out = {.n=4, .b={9,9,9,9}}, xy = {.n=2, .xy={77,77}};
    if (!strcmp(argv[2], "missing-original")) {
        (void)status(e,c); CHECK(exception);
        puts("MISSING_ORIGINAL_THROWS"); return 0;
    }
    if (!strcmp(argv[2], "constructor-reenter")) {
        (void)status(e,c); CHECK(exception);
        exception = 0; CHECK(status(e,c) == 0);
        puts("RESOLVE_REENTRY_REJECTED"); return 0;
    }
    CHECK(status(e,c) == 0);
    init(e,c,(jstring)1,(jstring)1,(jstring)1);
    if (!strcmp(argv[2], "query-throws")) {
        CHECK(exception); exception = 0;
        CHECK(status(e,c) == -1);
        puts("QUERY_EXCEPTION_PRESERVED"); return 0;
    }
    if (!strcmp(argv[2], "invalid")) {
        CHECK(status(e,c) == -1);
        run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out);
        CHECK(alpha(e,c) == JNI_TRUE && out.b[0] == 9);
        release(e,c); CHECK(status(e,c) == 0 && alpha(e,c) == JNI_FALSE);
        puts("INVALID_MASK_PRESERVES_ORIGINAL"); return 0;
    }
    CHECK(status(e,c) == 1);
    if (!strcmp(argv[2], "release-during-run")) {
        release_during_read = release;
        run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out);
        CHECK(!exception && out.b[0] == 9 && out.b[3] == 9);
        CHECK(status(e,c) == 0 && alpha(e,c) == JNI_FALSE);
        puts("RELEASE_DURING_RUN_INVALIDATES_RESULT"); return 0;
    }
    if (!strcmp(argv[2], "threaded-release")) {
        pthread_t worker;
        struct run_task task = {run, e, &in, &out};
        block_read = 1;
        CHECK(!pthread_create(&worker, NULL, run_in_thread, &task));
        pthread_mutex_lock(&test_lock);
        while (!read_entered) pthread_cond_wait(&test_cond, &test_lock);
        pthread_mutex_unlock(&test_lock);
        release(e,c);
        pthread_mutex_lock(&test_lock);
        allow_read = 1;
        pthread_cond_broadcast(&test_cond);
        pthread_mutex_unlock(&test_lock);
        CHECK(!pthread_join(worker, NULL));
        CHECK(!exception && out.b[0] == 9 && out.b[3] == 9);
        CHECK(status(e,c) == 0 && alpha(e,c) == JNI_FALSE);
        puts("THREADED_RELEASE_INVALIDATES_RESULT"); return 0;
    }
    if (!strcmp(argv[2], "overlapping-init") || !strcmp(argv[2], "release-during-init")) {
        void *original = dlopen(getenv("RME_TEST_ORIGINAL_PATH"), RTLD_NOW | RTLD_NOLOAD);
        CHECK(original);
        void (*wait_for_init)(void) = dlsym(original, "rme_test_wait_for_init");
        void (*continue_init)(void) = dlsym(original, "rme_test_continue_init");
        CHECK(wait_for_init && continue_init);
        pthread_t worker;
        struct init_task task = {init, e};
        CHECK(!pthread_create(&worker, NULL, init_in_thread, &task));
        wait_for_init();
        int releasing = !strcmp(argv[2], "release-during-init");
        if (releasing) release(e,c);
        else {
            init(e,c,(jstring)1,(jstring)1,(jstring)1);
            CHECK(status(e,c) == 1 && alpha(e,c) == JNI_FALSE);
        }
        continue_init();
        CHECK(!pthread_join(worker, NULL));
        if (releasing) CHECK(!exception && status(e,c) == -1 && alpha(e,c) == JNI_TRUE);
        else CHECK(!exception && status(e,c) == 1 && alpha(e,c) == JNI_FALSE);
        dlclose(original);
        puts(releasing ? "RELEASE_INVALIDATES_INFLIGHT_INIT" : "OLDER_INIT_CANNOT_OVERWRITE_NEWER_RESULT"); return 0;
    }
    run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out);
    CHECK(alpha(e,c) == JNI_FALSE && out.b[0] == 3 && out.b[1] == 2 && out.b[2] == 1);
    highest(e,c,(jintArray)&xy);
    if (!strcmp(argv[2], "original") || !strcmp(argv[2], "reenter")) {
        CHECK(out.b[3] == 255 && xy.xy[0] == 7 && xy.xy[1] == 8);
        in.b[0] = 0xee; run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out); CHECK(exception);
        exception = 0; release(e,c); CHECK(status(e,c) == 0);
        puts("ORIGINAL_DELEGATED"); return 0;
    }
    CHECK(out.b[3] == 128 && xy.xy[0] == 0 && xy.xy[1] == 0);
    init(e,c,(jstring)2,(jstring)1,(jstring)1);
    CHECK(exception); exception = 0; CHECK(status(e,c) == -1);
    init(e,c,(jstring)1,(jstring)1,(jstring)1); CHECK(status(e,c) == 1);
    run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out); CHECK(alpha(e,c) == JNI_FALSE);
    exception = 1; out.b[0] = 17; run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out);
    CHECK(exception && out.b[0] == 17); exception = 0; CHECK(alpha(e,c) == JNI_FALSE);
    fail_write = 1; run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out);
    CHECK(exception && out.b[0] == 17); exception = 0; fail_write = 0; CHECK(alpha(e,c) == JNI_TRUE);
    run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out); CHECK(alpha(e,c) == JNI_FALSE);
    in.b[0] = 7; run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out); CHECK(alpha(e,c) == JNI_TRUE);
    xy.xy[0] = 77; highest(e,c,(jintArray)&xy); CHECK(xy.xy[0] == 0 && xy.xy[1] == 0);
    release(e,c);
    if (!strcmp(argv[2], "no-so")) CHECK(status(e,c) == -1 && alpha(e,c) == JNI_TRUE);
    else CHECK(status(e,c) == 0 && alpha(e,c) == JNI_FALSE);
    init(e,c,(jstring)1,(jstring)1,(jstring)1); CHECK(status(e,c) == 1);
    in.b[0] = 1; run(e,c,(jbyteArray)&in,1,1,(jbyteArray)&out); CHECK(alpha(e,c) == JNI_FALSE);
    release(e,c); puts("FIXED_AND_RELEASE_BRANCH_OK"); return 0;
}
