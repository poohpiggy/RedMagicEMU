// SPDX-License-Identifier: MIT
#include <type_traits>
#include <android/native_window.h>
#include "gfxstream/host/native_sub_window.h"

static_assert(std::is_same<EGLNativeWindowType, ANativeWindow*>::value,
              "Android EGLNativeWindowType must be ANativeWindow*");
static_assert(std::is_same<FBNativeWindowType, ANativeWindow*>::value,
              "Android FBNativeWindowType must be ANativeWindow*");
