// Exercises the fixed upstream Android compile-time branch and runTask body.
#include <cassert>
#include <functional>
#include <future>
#include <memory>
#include "post_only.inc"
using UiUpdateFunc = void (*)(void*);
class PostWorker {
public:
    bool m_mainThreadPostingOnly = postOnlyOnMainThread();
    std::function<void(UiUpdateFunc, void*, bool)> m_runOnUiThread;
    void runTask(std::packaged_task<void()> task);
};
#include "run_task.inc"
int main() {
    assert(!postOnlyOnMainThread());
    PostWorker worker;
    bool called = false;
    worker.runTask(std::packaged_task<void()>([&] { called = true; }));
    assert(called); // Empty UI callback cannot be called on this path.
}
