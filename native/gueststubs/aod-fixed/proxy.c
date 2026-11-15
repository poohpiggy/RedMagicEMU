/*
 * Fixed-theme compatibility for the investigated NX809J firmware. A fresh
 * target import stages the verified build and alpha in its runtime copy.
 * The original interface has no request ID; concurrent call ordering and the
 * behavior of firmware revisions beyond the verified target remain limited.
 *
 * The production build supplies fixed runtime paths. Environment-based paths
 * and the tiny test image below are compiled only in synthetic host tests.
 */
#define _POSIX_C_SOURCE 200809L
#include <jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sha256.h"

#define PREFIX "Java_com_zte_saliency_SaliencyApi_"
#ifdef RME_AOD_TEST_FIXTURE
#define WIDTH 1
#define HEIGHT 1
static const uint8_t input_sha[32] = {
    0x03,0x90,0x58,0xc6,0xf2,0xc0,0xcb,0x49,0x2c,0x53,0x3b,0x0a,0x4d,0x14,0xef,0x77,
    0xcc,0x0f,0x78,0xab,0xcc,0xce,0xd5,0x28,0x7d,0x84,0xa1,0xa2,0x01,0x1c,0xfb,0x81,
};
static const uint8_t alpha_sha[32] = {
    0x76,0xbe,0x8b,0x52,0x8d,0x00,0x75,0xf7,0xaa,0xe9,0x8d,0x6f,0xa5,0x7a,0x6d,0x3c,
    0x83,0xae,0x48,0x0a,0x84,0x69,0xe6,0x68,0xd7,0xb0,0xaf,0x96,0x89,0x95,0xac,0x71,
};
static const char *original_path(void) { return getenv("RME_TEST_ORIGINAL_PATH"); }
static const char *alpha_path(void) { return getenv("RME_TEST_ALPHA_PATH"); }
#else
#if !defined(RME_AOD_ORIGINAL_PATH) || !defined(RME_AOD_ALPHA_PATH)
#error "The experimental library requires explicitly supplied runtime paths"
#endif
#define WIDTH 1216
#define HEIGHT 2688
static const uint8_t input_sha[32] = {
    0x67,0xcc,0xd4,0x17,0xf2,0x09,0xd9,0xd2,0x90,0x40,0x13,0xb0,0x15,0x84,0x56,0x99,
    0x7d,0xd0,0xdb,0x8f,0x52,0xf7,0xe6,0x80,0x6e,0xd5,0x05,0x40,0xf4,0x21,0xf6,0x36,
};
static const uint8_t alpha_sha[32] = {
    0x01,0x9b,0xfe,0xea,0x5f,0x8e,0x51,0x09,0xbe,0xfa,0x23,0x1f,0x8f,0xeb,0xce,0x04,
    0x73,0x23,0x20,0xff,0x1e,0x25,0x6e,0x51,0xfa,0x28,0xb0,0x92,0xe6,0x23,0x1c,0x7a,
};
static const char *original_path(void) { return RME_AOD_ORIGINAL_PATH; }
static const char *alpha_path(void) { return RME_AOD_ALPHA_PATH; }
#endif
#define PIXELS ((size_t)WIDTH * (size_t)HEIGHT)

typedef void (*init_fn)(JNIEnv *, jclass, jstring, jstring, jstring);
typedef jint (*status_fn)(JNIEnv *, jclass);
typedef void (*run_fn)(JNIEnv *, jclass, jbyteArray, jint, jint, jbyteArray);
typedef jboolean (*alpha_fn)(JNIEnv *, jclass);
typedef void (*highest_fn)(JNIEnv *, jclass, jintArray);
typedef void (*release_fn)(JNIEnv *, jclass);
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapInit(JNIEnv *, jclass, jstring, jstring, jstring);
JNIEXPORT jint JNICALL Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(JNIEnv *, jclass);
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(JNIEnv *, jclass, jbyteArray, jint, jint, jbyteArray);
JNIEXPORT jboolean JNICALL Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(JNIEnv *, jclass);
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(JNIEnv *, jclass, jintArray);
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRelease(JNIEnv *, jclass);
static init_fn orig_init;
static status_fn orig_status;
static run_fn orig_run;
static alpha_fn orig_alpha;
static highest_fn orig_highest;
static release_fn orig_release;
static const unsigned char *orig_so_exists;
static int resolved;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static _Thread_local int resolving;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Last-result state is process-global, as in the inspected vendor library.
 * The lock guards only proxy state, never a call into vendor code or JNI. */
static int fixed_active, virtual_result, result_null, point_valid;
static jint top_x, top_y;
static uint8_t *mask;
static uint64_t generation = 1;

/* Zero permanently disables the fixed backend if the counter is exhausted. */
static uint64_t advance_generation(void) {
    if (generation) generation = generation == UINT64_MAX ? 0 : generation + 1;
    return generation;
}

static void resolve_once(void) {
    const char *path = original_path();
    if (!path) return;
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) return;
    orig_init = (init_fn)dlsym(h, PREFIX "saliencyMapInit");
    orig_status = (status_fn)dlsym(h, PREFIX "saliencyInitStatus");
    orig_run = (run_fn)dlsym(h, PREFIX "saliencyMapRunning");
    orig_alpha = (alpha_fn)dlsym(h, PREFIX "saliencyIsAlphaNull");
    orig_highest = (highest_fn)dlsym(h, PREFIX "saliencyMapHighestPointGet");
    orig_release = (release_fn)dlsym(h, PREFIX "saliencyMapRelease");
    /* This exported byte controls the target's release branch. Its absence
     * disables the fixed backend, while still allowing original delegation. */
    orig_so_exists = (const unsigned char *)dlsym(h, "isSoFileExist");
    if (!orig_init || !orig_status || !orig_run || !orig_alpha || !orig_highest || !orig_release ||
        orig_init == Java_com_zte_saliency_SaliencyApi_saliencyMapInit ||
        orig_status == Java_com_zte_saliency_SaliencyApi_saliencyInitStatus ||
        orig_run == Java_com_zte_saliency_SaliencyApi_saliencyMapRunning ||
        orig_alpha == Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull ||
        orig_highest == Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet ||
        orig_release == Java_com_zte_saliency_SaliencyApi_saliencyMapRelease) {
        dlclose(h); return;
    }
    /* Keep the handle alive while JNI methods may be called. */
    resolved = 1;
}
static int resolve(void) {
    /* A vendor constructor may synchronously call back into this proxy.
     * Recursive pthread_once on the same control object would deadlock. */
    if (resolving) return 0;
    resolving = 1;
    int error = pthread_once(&once, resolve_once);
    resolving = 0;
    return !error && resolved;
}
static int resolve_or_throw(JNIEnv *e) {
    if (resolve()) return 1;
    if (!(*e)->ExceptionCheck(e)) {
        jclass error = (*e)->FindClass(e, "java/lang/UnsatisfiedLinkError");
        if (error) {
            (*e)->ThrowNew(e, error, "Original saliency JNI unavailable");
            (*e)->DeleteLocalRef(e, error);
        }
    }
    return 0;
}

static int load_mask(uint8_t **loaded, jint *x, jint *y) {
    const char *path = alpha_path();
    if (!path) return 0;
    /* Avoid following the final link or blocking on a FIFO. Parent-directory
     * identity and replacement races remain an integration responsibility. */
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size != (off_t)PIXELS) {
        close(fd); return 0;
    }
    uint8_t *data = malloc(PIXELS), digest[32];
    if (!data) { close(fd); return 0; }
    size_t n = 0;
    while (n < PIXELS) {
        ssize_t got = read(fd, data + n, PIXELS - n);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { free(data); close(fd); return 0; }
        n += (size_t)got;
    }
    uint8_t extra;
    ssize_t end;
    do { end = read(fd, &extra, 1); } while (end < 0 && errno == EINTR);
    int close_failed = close(fd);
    if (end != 0 || close_failed) { free(data); return 0; }
    rme_sha256(data, PIXELS, digest);
    if (memcmp(digest, alpha_sha, sizeof(digest))) { free(data); return 0; }
    size_t first = 0;
    while (first < PIXELS && !data[first]) first++;
    if (first == PIXELS) { free(data); return 0; }
    *x = (jint)(first % WIDTH); *y = (jint)(first / WIDTH);
    *loaded = data;
    return 1;
}

JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapInit(
    JNIEnv *e, jclass c, jstring a, jstring b, jstring d) {
    if (!resolve_or_throw(e)) return;
    if (!a || !b || !d) { orig_init(e, c, a, b, d); return; }
    pthread_mutex_lock(&lock);
    uint64_t current = advance_generation();
    free(mask); mask = NULL; fixed_active = virtual_result = point_valid = 0;
    pthread_mutex_unlock(&lock);
    orig_init(e, c, a, b, d);
    if ((*e)->ExceptionCheck(e)) return;
    /* Preserve every original status, including -1, if the fixed asset is unavailable. */
    jint status = orig_status(e, c);
    if ((*e)->ExceptionCheck(e)) return;
    uint8_t *candidate = NULL;
    jint x = 0, y = 0;
    if (current && status != 1 && orig_so_exists && load_mask(&candidate, &x, &y)) {
        jboolean previous_null = orig_alpha(e, c);
        if ((*e)->ExceptionCheck(e)) {
            free(candidate); return;
        }
        pthread_mutex_lock(&lock);
        if (generation == current) {
            mask = candidate; candidate = NULL;
            top_x = x; top_y = y;
            result_null = previous_null == JNI_TRUE;
            fixed_active = virtual_result = 1;
        }
        pthread_mutex_unlock(&lock);
        free(candidate);
    }
}
JNIEXPORT jint JNICALL Java_com_zte_saliency_SaliencyApi_saliencyInitStatus(JNIEnv *e, jclass c) {
    if (!resolve_or_throw(e)) return 0;
    pthread_mutex_lock(&lock);
    int active = fixed_active;
    pthread_mutex_unlock(&lock);
    return active ? 1 : orig_status(e, c);
}
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRunning(
    JNIEnv *e, jclass c, jbyteArray input, jint w, jint h, jbyteArray output) {
    if (!resolve_or_throw(e)) return;
    if ((*e)->ExceptionCheck(e)) return;
    pthread_mutex_lock(&lock);
    if (!fixed_active) {
        virtual_result = 0;
        pthread_mutex_unlock(&lock);
        orig_run(e, c, input, w, h, output); return;
    }
    uint64_t current = generation;
    uint8_t *alpha = malloc(PIXELS);
    if (alpha) memcpy(alpha, mask, PIXELS);
    result_null = 1;
    pthread_mutex_unlock(&lock);
    if (!alpha || !input || !output || w != WIDTH || h != HEIGHT || (*e)->IsSameObject(e, input, output)) goto done;
    if ((*e)->GetArrayLength(e, input) != (jsize)(PIXELS * 3) || (*e)->ExceptionCheck(e) ||
        (*e)->GetArrayLength(e, output) != (jsize)(PIXELS * 4) || (*e)->ExceptionCheck(e)) goto done;
    jbyte *rgb = malloc(PIXELS * 3), *bgra = malloc(PIXELS * 4);
    if (!rgb || !bgra) { free(rgb); free(bgra); goto done; }
    (*e)->GetByteArrayRegion(e, input, 0, (jsize)(PIXELS * 3), rgb);
    if (!(*e)->ExceptionCheck(e)) {
        uint8_t digest[32]; rme_sha256((const uint8_t *)rgb, PIXELS * 3, digest);
        if (!memcmp(digest, input_sha, sizeof(digest))) {
            for (size_t i = 0; i < PIXELS; ++i) {
                bgra[i*4] = rgb[i*3+2]; bgra[i*4+1] = rgb[i*3+1];
                bgra[i*4+2] = rgb[i*3]; bgra[i*4+3] = (jbyte)alpha[i];
            }
            pthread_mutex_lock(&lock);
            int valid = generation == current && fixed_active;
            pthread_mutex_unlock(&lock);
            if (valid) {
                (*e)->SetByteArrayRegion(e, output, 0, (jsize)(PIXELS * 4), bgra);
                if (!(*e)->ExceptionCheck(e)) {
                    pthread_mutex_lock(&lock);
                    if (generation == current && fixed_active) { result_null = 0; point_valid = 1; }
                    pthread_mutex_unlock(&lock);
                }
            }
        }
    }
    free(rgb); free(bgra);
done:
    free(alpha);
}
JNIEXPORT jboolean JNICALL Java_com_zte_saliency_SaliencyApi_saliencyIsAlphaNull(JNIEnv *e, jclass c) {
    if (!resolve_or_throw(e)) return JNI_TRUE;
    pthread_mutex_lock(&lock);
    int use_fixed = virtual_result;
    jboolean result = result_null ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&lock);
    return use_fixed ? result : orig_alpha(e, c);
}
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapHighestPointGet(JNIEnv *e, jclass c, jintArray p) {
    if (!resolve_or_throw(e)) return;
    pthread_mutex_lock(&lock);
    int use_fixed = virtual_result, valid = point_valid;
    const jint xy[2] = {top_x, top_y};
    pthread_mutex_unlock(&lock);
    if (!use_fixed) orig_highest(e, c, p);
    else if (valid && p && !(*e)->ExceptionCheck(e) &&
             (*e)->GetArrayLength(e, p) >= 2 && !(*e)->ExceptionCheck(e))
        (*e)->SetIntArrayRegion(e, p, 0, 2, xy);
}
JNIEXPORT void JNICALL Java_com_zte_saliency_SaliencyApi_saliencyMapRelease(JNIEnv *e, jclass c) {
    if (!resolve_or_throw(e)) return;
    pthread_mutex_lock(&lock);
    int original_so_present = orig_so_exists && *orig_so_exists;
    advance_generation();
    if (fixed_active && original_so_present) { result_null = 0; point_valid = 0; }
    fixed_active = 0;
    free(mask); mask = NULL;
    pthread_mutex_unlock(&lock);
    orig_release(e, c);
}
