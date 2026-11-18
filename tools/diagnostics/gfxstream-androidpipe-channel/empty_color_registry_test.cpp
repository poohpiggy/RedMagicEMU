// Test-only substitute for cases that never enable ColorBuffer callbacks.
#include "host_color_registry.h"
namespace rme {
std::shared_ptr<void> findRegisteredColorBuffer(uint32_t) { return {}; }
}
