#pragma once
#include <memory>

namespace rme {
class UiDispatcher {
public:
    using Callback = void (*)(void*);
    UiDispatcher();
    ~UiDispatcher();
    UiDispatcher(const UiDispatcher&) = delete;
    UiDispatcher& operator=(const UiDispatcher&) = delete;
    // Bind to the calling thread. On Android the owner must verify main Looper.
    bool initialize();
    bool isUiThread() const;
    int fd() const;
    bool post(Callback, void*, bool wait);
    // Execute pending callbacks; owner thread only.
    bool drain();
    // Owner thread: after the renderer has stopped, prevent further posts.
    bool quiesce();
    // Owner thread only, and only once all callbacks and producers have stopped.
    bool shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
