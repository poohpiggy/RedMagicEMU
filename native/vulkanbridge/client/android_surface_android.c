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
    return native_window_api_connect(window, NATIVE_WINDOW_API_EGL);
}
static int disconnect_window(struct ANativeWindow *window) {
    return native_window_api_disconnect(window, NATIVE_WINDOW_API_EGL);
}
const struct RmeAndroidWindowOps *rme_android_window_ops(void) {
    static const struct RmeAndroidWindowOps ops = { retain, release, usage, connect_window, disconnect_window };
    return &ops;
}
