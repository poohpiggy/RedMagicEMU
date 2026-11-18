#include "../surface_jni.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

struct ANativeWindow { int refs; };
static struct ANativeWindow window;
ANativeWindow* ANativeWindow_fromSurface(JNIEnv* env, jobject surface) {
    assert(env && surface);
    ++window.refs;
    return &window;
}
void ANativeWindow_acquire(ANativeWindow* native) { ++native->refs; }
void ANativeWindow_release(ANativeWindow* native) { assert(native->refs > 0); --native->refs; }
extern int rme_surface_cpp_probe(JNIEnv* env, jobject surface);
int main(void) {
    assert(rme_surface_cpp_probe((JNIEnv*)(uintptr_t)1, (jobject)(uintptr_t)1) == 0);
    assert(window.refs == 0);
    puts("CPP_DECODER_TO_C_SURFACE_LINK_OK");
}
