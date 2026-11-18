#include "pipe_principal_registry.h"
#include <cassert>
#include <memory>
#include <thread>

int main() {
    rme::PipePrincipalRegistry registry;
    int pipe = 0, other = 0;
    auto a = std::make_shared<const rme::PipePrincipal>();
    auto b = std::make_shared<const rme::PipePrincipal>();
    assert(!registry.lookup(&pipe));
    assert(!registry.bind(nullptr, a));
    assert(!registry.bind(&pipe, {}));
    auto first = registry.bind(&pipe, a);
    assert(first && registry.lookup(&pipe) == a);
    assert(!registry.bind(&pipe, b));
    auto same_owner = registry.bind(&other, a);
    assert(same_owner && registry.lookup(&other) == a);
    auto captured = registry.lookup(&pipe);
    std::weak_ptr<const rme::PipePrincipal> lifetime = a;
    first.reset();
    assert(!registry.lookup(&pipe));
    auto second = registry.bind(&pipe, b);
    assert(second && registry.lookup(&pipe) == b && captured == a);
    same_owner.reset(); a.reset();
    assert(!lifetime.expired());
    captured.reset();
    assert(lifetime.expired());
    second.reset();
    assert(!registry.lookup(&pipe));
    auto concurrent = [&](const std::shared_ptr<const rme::PipePrincipal>& principal) {
        for (int i = 0; i < 1000; ++i) {
            auto binding = registry.bind(&pipe, principal);
            if (binding) {
                auto current = registry.lookup(&pipe);
                assert(current == principal);
            }
        }
    };
    std::thread x(concurrent, b), y(concurrent, b);
    x.join(); y.join();
    assert(!registry.lookup(&pipe));
}
