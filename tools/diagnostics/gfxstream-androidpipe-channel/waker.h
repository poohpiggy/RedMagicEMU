#pragma once
#include <functional>
#include <cstdint>
namespace android {
class VmLock;
// Callback state must outlive AndroidPipe's process-global wake runner.
struct RmePipeTimer {
    void* context;
    void (*install)(void*, std::function<void()>);
    void (*uninstall)(void*);
    void (*schedule)(void*, uint64_t);
};
void rmeInitPipeWaker(VmLock*, const RmePipeTimer&);
}
