#include "principal_scope.h"
#include <atomic>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <thread>

using rme::Principal;
using rme::PrincipalScope;

int main() {
    auto a = std::make_shared<const Principal>();
    auto b = std::make_shared<const Principal>();
    assert(!PrincipalScope::take());
    {
        PrincipalScope outer(a);
        { PrincipalScope inner(b); assert(PrincipalScope::take() == b); assert(!PrincipalScope::take()); }
        assert(PrincipalScope::take() == a);
        assert(!PrincipalScope::take());
    }
    assert(!PrincipalScope::take());
    {
        PrincipalScope outer(a);
        { PrincipalScope empty({}); assert(!PrincipalScope::take()); }
        try { PrincipalScope inner(b); throw std::runtime_error("unwind"); }
        catch (const std::runtime_error&) {}
        assert(PrincipalScope::take() == a);
    }
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    auto worker = [&](const std::shared_ptr<const Principal>& p) {
        PrincipalScope scope(p);
        ++ready;
        while (!start.load()) std::this_thread::yield();
        assert(PrincipalScope::take() == p);
        assert(!PrincipalScope::take());
    };
    std::thread first(worker, a), second(worker, b);
    while (ready.load() != 2) std::this_thread::yield();
    start = true;
    first.join(); second.join();
    assert(!PrincipalScope::take());
    std::weak_ptr<const Principal> retained;
    std::shared_ptr<const Principal> captured;
    {
        auto p = std::make_shared<const Principal>();
        retained = p;
        { PrincipalScope scope(p); captured = PrincipalScope::take(); }
        p.reset();
        assert(!retained.expired());
    }
    captured.reset();
    assert(retained.expired());
}
