#include "ui_dispatcher.h"
#include <atomic>
#include <cassert>
#include <poll.h>
#include <thread>
#include <cstdio>

struct State {
    rme::UiDispatcher* dispatcher;
    std::thread::id ui;
    std::atomic<int> calls{0};
};
static void callback(void* data) {
    auto* state = static_cast<State*>(data);
    assert(state->dispatcher->isUiThread());
    assert(std::this_thread::get_id() == state->ui);
    ++state->calls;
}
int main() {
    rme::UiDispatcher dispatcher;
    State state{&dispatcher, std::this_thread::get_id()};
    assert(!dispatcher.post(callback, &state, false));
    assert(dispatcher.initialize() && dispatcher.isUiThread());
    std::atomic<bool> returned{false};
    std::thread worker([&] {
        assert(!dispatcher.isUiThread());
        assert(!dispatcher.drain() && !dispatcher.quiesce());
        assert(dispatcher.post(callback, &state, true));
        returned = true;
    });
    pollfd p{dispatcher.fd(), POLLIN, 0};
    assert(poll(&p, 1, 1000) == 1);
    assert(!returned && !dispatcher.shutdown());
    assert(dispatcher.drain());
    worker.join();
    assert(returned && state.calls == 1);
    std::thread async([&] { assert(dispatcher.post(callback, &state, false)); });
    async.join();
    assert(poll(&p, 1, 1000) == 1 && dispatcher.drain());
    assert(state.calls == 2);
    assert(dispatcher.post(callback, &state, true));
    assert(state.calls == 3);
    std::thread batch([&] {
        for (int i = 0; i < 80; ++i) assert(dispatcher.post(callback, &state, false));
    });
    batch.join();
    assert(poll(&p, 1, 1000) == 1 && dispatcher.drain());
    assert(state.calls == 67);
    assert(poll(&p, 1, 1000) == 1 && dispatcher.drain());
    assert(state.calls == 83);
    assert(dispatcher.quiesce());
    assert(!dispatcher.post(callback, &state, false));
    assert(dispatcher.shutdown());
    assert(!dispatcher.post(callback, &state, false));
    std::puts("UI_DISPATCH_MAIN_THREAD_SYNC_ASYNC_OK");
}
