/* Uses Android native-window operations; does not create or present images. */
#include "android_surface.h"
#include <android/native_window.h>
#include <system/window.h>

static void retain(struct ANativeWindow *window) { ANativeWindow_acquire(window); }
static void release(struct ANativeWindow *window) { ANativeWindow_release(window); }
static int usage(struct ANativeWindow *window, uint64_t *value) {
    return native_window_get_consumer_usage(window, value);
}
static int connect_window(struct ANativeWindow *window) {
#ifdef RME_TEST_ANDROID_READBACK_WSI
    /* ANativeWindow_lock establishes the CPU connection on first present. */
    (void)window;
    return 0;
#else
    return native_window_api_connect(window, NATIVE_WINDOW_API_EGL);
#endif
}
static int disconnect_window(struct ANativeWindow *window) {
#ifdef RME_TEST_ANDROID_READBACK_WSI
    return native_window_api_disconnect(window, NATIVE_WINDOW_API_CPU);
#else
    return native_window_api_disconnect(window, NATIVE_WINDOW_API_EGL);
#endif
}
const struct RmeAndroidWindowOps *rme_android_window_ops(void) {
    static const struct RmeAndroidWindowOps ops = { retain, release, usage, connect_window, disconnect_window };
    return &ops;
}
