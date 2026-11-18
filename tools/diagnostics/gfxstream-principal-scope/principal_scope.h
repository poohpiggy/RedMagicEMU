#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace rme {
// Host-private identity token. Its creator is responsible for authenticating
// and pinning the associated client; this type performs no authentication.
struct Principal final {
    Principal() = default;
    Principal(const Principal&) = delete;
    Principal& operator=(const Principal&) = delete;

    // Only the host session which read this ID from GLProcessPipe may bind it.
    bool bind(uint64_t id) const {
        uint64_t pending = 0;
        return id && id != UINT64_MAX && puid_.compare_exchange_strong(pending, id);
    }
    uint64_t puid() const {
        const auto id = puid_.load();
        return id == UINT64_MAX ? 0 : id;
    }
    void revoke() const { puid_.store(UINT64_MAX); }
    using Drain = void (*)(const std::shared_ptr<void>&);
    void trackWorker(const std::shared_ptr<void>& worker, Drain drain) const;
    void drainWorkers() const;

private:
    mutable std::atomic<uint64_t> puid_{0};
    mutable std::vector<std::pair<std::weak_ptr<void>, Drain>> workers_;
};

// Owner-thread-only, noncopyable stack scope. The nearest scope masks all
// outer scopes, even if empty or already consumed. A successful take consumes
// it exactly once; nested scopes restore the previous scope on destruction.
class PrincipalScope final {
public:
    explicit PrincipalScope(std::shared_ptr<const Principal> principal);
    ~PrincipalScope();
    PrincipalScope(const PrincipalScope&) = delete;
    PrincipalScope& operator=(const PrincipalScope&) = delete;
    static std::shared_ptr<const Principal> take();
    static std::shared_ptr<const Principal> peek();
private:
    static thread_local PrincipalScope* current_;
    PrincipalScope* previous_;
    std::shared_ptr<const Principal> principal_;
};
}
