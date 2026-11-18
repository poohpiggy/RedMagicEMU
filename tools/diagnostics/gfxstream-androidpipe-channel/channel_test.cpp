// Exercises the real upstream AndroidPipe service registry without a GPU.
#include "channel.h"
#include "host-common/opengl/GLProcessPipe.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <unordered_set>

static std::unordered_set<uint64_t> process_ids() {
    std::unordered_set<uint64_t> ids;
    android::opengl::forEachProcessPipeId([&](uint64_t id) { ids.insert(id); });
    return ids;
}

int main() {
    using namespace rme;
    Device d;
    assert(d.initialize());
    assert(d.wakeupFd() >= 0 && d.dispatchWakeups());
    Device other;
    assert(!other.initialize());
    assert(!other.open(Service::Process));
    auto first = d.open(Service::Process);
    auto second = d.open(Service::Process);
    assert(first && second);
    int32_t invalid = 99, confirm = 100;
    assert(first->send(&invalid, sizeof(invalid)) < 0);
    assert(first->send(&confirm, 1) < 0);
    assert(first->send(&confirm, sizeof(confirm)) == 4);
    assert(first->send(&confirm, sizeof(confirm)) < 0);
    uint64_t a = 0, b = 0;
    assert(first->receive(&a, 4) < 0);
    assert(first->receive(&a, sizeof(a)) == 8);
    assert(first->receive(&a, sizeof(a)) < 0);
    assert(second->send(&confirm, sizeof(confirm)) == 4);
    assert(second->receive(&b, sizeof(b)) == 8);
    assert(a && b && a != b);
    assert(process_ids() == (std::unordered_set<uint64_t>{a, b}));
    auto third = d.open(Service::Process);
    assert(third);
    alignas(int32_t) uint8_t bytes[8] = {};
    std::memcpy(bytes + 1, &confirm, sizeof(confirm));
    assert(third->send(bytes + 1, sizeof(confirm)) == 4);
    uint64_t c = 0;
    assert(third->receive(&c, sizeof(c)) == 8 && c && c != b);
    third.reset();
    assert(process_ids() == (std::unordered_set<uint64_t>{a, b}));
    int cross_thread = 0;
    std::thread t([&] { cross_thread = second->send(&confirm, sizeof(confirm)); });
    t.join();
    assert(cross_thread < 0);
    first.reset();
    assert(process_ids() == (std::unordered_set<uint64_t>{b}));
    second.reset();
    assert(process_ids().empty());
    // No renderer was initialized: the real upstream GLES service must refuse.
    assert(!d.open(Service::OpenGLES));
    std::puts("REAL_PROCESS_PIPE_OK RENDERER_ABSENCE_REJECTED");
}
