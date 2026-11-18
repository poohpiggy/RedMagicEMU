#pragma once
#include <jni.h>
struct QAndroidEmulatorWindowAgent;
namespace rme {
// Invoke on Android's main Java Looper, before starting the renderer.
bool attachAndroidUiAgent(JNIEnv* env);
// Valid only until detach; other window callbacks remain unavailable.
const QAndroidEmulatorWindowAgent* androidUiAgent();
// Invoke on the main Looper after stopping/joining all renderer producers.
bool detachAndroidUiAgent();
}
