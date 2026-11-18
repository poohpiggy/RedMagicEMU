#ifndef TEST_ANDROID_NATIVE_WINDOW_H
#define TEST_ANDROID_NATIVE_WINDOW_H
typedef struct ANativeWindow { int refs; } ANativeWindow;
void ANativeWindow_acquire(ANativeWindow* window);
void ANativeWindow_release(ANativeWindow* window);
#endif
