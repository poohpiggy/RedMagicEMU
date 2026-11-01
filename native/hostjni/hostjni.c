// Хост-помощник (arm64): ослабляет fdsan в процессе :vm.
// Закрытый libglbridge делает freopen() поверх fd 2, которым на Android 10+ владеет unique_fd,
// и fdsan роняет всё приложение. Переводим fdsan в режим «предупредить один раз».
#include <jni.h>
#include <dlfcn.h>
#include <sys/resource.h>

#ifndef ANDROID_FDSAN_ERROR_LEVEL_DISABLED
enum android_fdsan_error_level {
    ANDROID_FDSAN_ERROR_LEVEL_DISABLED = 0,
    ANDROID_FDSAN_ERROR_LEVEL_WARN_ONCE,
    ANDROID_FDSAN_ERROR_LEVEL_WARN_ALWAYS,
    ANDROID_FDSAN_ERROR_LEVEL_FATAL,
};
#endif

JNIEXPORT void JNICALL Java_app_aemu_core_HostNative_relaxFdsan(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    enum android_fdsan_error_level (*set_level)(enum android_fdsan_error_level) =
        (enum android_fdsan_error_level (*)(enum android_fdsan_error_level))dlsym(RTLD_DEFAULT, "android_fdsan_set_error_level");
    if (set_level) {
        set_level(ANDROID_FDSAN_ERROR_LEVEL_DISABLED);
    }
}

// Гостевые процессы наследуют RLIMIT_STACK процесса :vm (у приложения он огромный). bionic 6.0 считает
// низ стека главного потока как «верх [stack] минус RLIMIT_STACK»; с лимитом ~2 ГБ ART получает
// нелепые границы стека и падает. 8 МБ — как у обычного процесса Android.
#include <sys/resource.h>
JNIEXPORT void JNICALL Java_app_aemu_core_HostNative_limitStack(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    struct rlimit r;
    if (getrlimit(RLIMIT_STACK, &r) == 0 && (r.rlim_cur == RLIM_INFINITY || r.rlim_cur > 8u << 20)) {
        r.rlim_cur = 8u << 20;
        setrlimit(RLIMIT_STACK, &r);
    }
}

// AF_UNIX datagram sockets for the emulated wpa_supplicant control interface (WifiStub.kt). The guest's libwpa_client
// (wpa_ctrl.c, CONFIG_CTRL_IFACE_UNIX) binds its own end and sends commands as datagrams; the answer goes back to the
// address each one came from, which android.system.Os cannot report for AF_UNIX (its recvfrom takes InetSocketAddress).
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stddef.h>

static int unaddr(JNIEnv *env, jstring path, struct sockaddr_un *a) {
    const char *p = (*env)->GetStringUTFChars(env, path, 0);
    if (!p) return -1;
    size_t n = strlen(p);
    memset(a, 0, sizeof *a);
    a->sun_family = AF_UNIX;
    int ok = n < sizeof a->sun_path;
    if (ok) memcpy(a->sun_path, p, n + 1);
    (*env)->ReleaseStringUTFChars(env, path, p);
    return ok ? (int)(offsetof(struct sockaddr_un, sun_path) + n + 1) : -1;
}

JNIEXPORT jint JNICALL Java_app_aemu_core_HostNative_dgramBind(JNIEnv *env, jclass cls, jstring path) {
    (void)cls;
    struct sockaddr_un a;
    int l = unaddr(env, path, &a);
    if (l < 0) return -ENAMETOOLONG;
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;
    unlink(a.sun_path);
    if (bind(fd, (struct sockaddr *)&a, (socklen_t)l) < 0) { int e = errno; close(fd); return -e; }
    chmod(a.sun_path, 0666);
    return fd;
}

// blocks; returns the length (0 after dgramClose), from[0] = the sender's path ("" for an unbound sender)
JNIEXPORT jint JNICALL Java_app_aemu_core_HostNative_dgramRecv(JNIEnv *env, jclass cls, jint fd, jbyteArray buf, jobjectArray from) {
    (void)cls;
    char b[4096];
    struct sockaddr_un a; socklen_t al = sizeof a;
    memset(&a, 0, sizeof a);
    ssize_t n;
    do n = recvfrom(fd, b, sizeof b, 0, (struct sockaddr *)&a, &al); while (n < 0 && errno == EINTR);
    if (n < 0) return -errno;
    jsize cap = (*env)->GetArrayLength(env, buf);
    if (n > cap) n = cap;
    (*env)->SetByteArrayRegion(env, buf, 0, (jsize)n, (const jbyte *)b);
    a.sun_path[sizeof a.sun_path - 1] = 0;
    (*env)->SetObjectArrayElement(env, from, 0, (*env)->NewStringUTF(env, al > offsetof(struct sockaddr_un, sun_path) ? a.sun_path : ""));
    return (jint)n;
}

JNIEXPORT jint JNICALL Java_app_aemu_core_HostNative_dgramSend(JNIEnv *env, jclass cls, jint fd, jbyteArray data, jstring to) {
    (void)cls;
    struct sockaddr_un a;
    int l = unaddr(env, to, &a);
    if (l < 0) return -ENAMETOOLONG;
    jsize n = (*env)->GetArrayLength(env, data);
    jbyte *d = (*env)->GetByteArrayElements(env, data, 0);
    ssize_t r = sendto(fd, d, (size_t)n, MSG_DONTWAIT, (struct sockaddr *)&a, (socklen_t)l);
    (*env)->ReleaseByteArrayElements(env, data, d, JNI_ABORT);
    return r < 0 ? -errno : (jint)r;
}

// wakes a dgramRecv blocked on another thread, then frees the descriptor
JNIEXPORT void JNICALL Java_app_aemu_core_HostNative_dgramClose(JNIEnv *env, jclass cls, jint fd) {
    (void)env; (void)cls;
    shutdown(fd, SHUT_RDWR);
    close(fd);
}
