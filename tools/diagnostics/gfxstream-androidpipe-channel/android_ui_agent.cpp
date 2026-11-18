#include "android_ui_agent.h"
#include "ui_dispatcher.h"
#include "host-common/window_agent.h"
#include <android/looper.h>
#include <atomic>
#include <exception>

namespace rme {
namespace {
UiDispatcher& dispatcher() { static auto* d = new UiDispatcher; return *d; }
ALooper* looper = nullptr; // attach/detach occur on the main Looper only.
std::atomic<bool> registered{false};
void runOnUiThread(UiUpdateFunc func, void* data, bool wait) {
    if (!registered.load() || !dispatcher().post(func, data, wait)) std::terminate();
}
bool isRunningInUiThread() { return registered.load() && dispatcher().isUiThread(); }
int onEvent(int, int events, void*) {
    if (events & (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP | ALOOPER_EVENT_INVALID)) std::terminate();
    if ((events & ALOOPER_EVENT_INPUT) && !dispatcher().drain()) std::terminate();
    return 1;
}
bool onMainJavaLooper(JNIEnv* env) {
    if (!env) return false;
    jclass type = env->FindClass("android/os/Looper");
    if (!type || env->ExceptionCheck()) return false;
    jmethodID currentMethod = env->GetStaticMethodID(type, "myLooper", "()Landroid/os/Looper;");
    jmethodID mainMethod = env->GetStaticMethodID(type, "getMainLooper", "()Landroid/os/Looper;");
    if (!currentMethod || !mainMethod || env->ExceptionCheck()) { env->DeleteLocalRef(type); return false; }
    jobject current = env->CallStaticObjectMethod(type, currentMethod);
    jobject main = env->ExceptionCheck() ? nullptr : env->CallStaticObjectMethod(type, mainMethod);
    bool valid = !env->ExceptionCheck() && current && main && env->IsSameObject(current, main);
    if (current) env->DeleteLocalRef(current);
    if (main) env->DeleteLocalRef(main);
    env->DeleteLocalRef(type);
    return valid;
}
}
bool attachAndroidUiAgent(JNIEnv* env) {
    if (registered.load() || !onMainJavaLooper(env)) return false;
    ALooper* current = ALooper_forThread();
    if (!current || !dispatcher().initialize()) return false;
    ALooper_acquire(current);
    if (ALooper_addFd(current, dispatcher().fd(), ALOOPER_POLL_CALLBACK,
                      ALOOPER_EVENT_INPUT, onEvent, nullptr) != 1) {
        ALooper_release(current);
        if (!dispatcher().shutdown()) std::terminate();
        return false;
    }
    looper = current;
    registered.store(true);
    return true;
}
const QAndroidEmulatorWindowAgent* androidUiAgent() {
    static const QAndroidEmulatorWindowAgent agent = [] {
        QAndroidEmulatorWindowAgent value{};
        value.runOnUiThread = runOnUiThread;
        value.isRunningInUiThread = isRunningInUiThread;
        return value;
    }();
    return registered.load() ? &agent : nullptr;
}
bool detachAndroidUiAgent() {
    if (!registered.load() || !dispatcher().isUiThread() || !dispatcher().quiesce()) return false;
    if (ALooper_removeFd(looper, dispatcher().fd()) != 1) return false;
    registered.store(false);
    if (!dispatcher().shutdown()) std::terminate();
    ALooper_release(looper);
    looper = nullptr;
    return true;
}
}
