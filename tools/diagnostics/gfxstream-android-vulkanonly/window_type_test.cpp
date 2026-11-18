#include "GlesCompat.h"
#include "render-utils/render_api_platform_types.h"
#include <type_traits>
static_assert(std::is_same<EGLNativeWindowType, FBNativeWindowType>::value,
              "Android Vulkan-only window must retain its native pointer type");
