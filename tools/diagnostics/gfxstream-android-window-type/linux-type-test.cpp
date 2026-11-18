// SPDX-License-Identifier: MIT
#include <type_traits>
#include "gfxstream/host/native_sub_window.h"

static_assert(std::is_same<EGLNativeWindowType, void*>::value,
              "Linux no-GLES window type must remain void*");
