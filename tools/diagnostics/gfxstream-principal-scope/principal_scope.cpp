#include "principal_scope.h"
#include <algorithm>
#include <exception>
#include <utility>

namespace rme {
void Principal::trackWorker(const std::shared_ptr<void>& worker, Drain drain) const {
    workers_.erase(std::remove_if(workers_.begin(), workers_.end(),
        [](const auto& entry) { return entry.first.expired(); }), workers_.end());
    workers_.emplace_back(worker, drain);
}

void Principal::drainWorkers() const {
    for (const auto& [worker, drain] : workers_) {
        if (const auto live = worker.lock()) drain(live);
    }
    workers_.clear();
}

thread_local PrincipalScope* PrincipalScope::current_ = nullptr;
PrincipalScope::PrincipalScope(std::shared_ptr<const Principal> principal)
    : previous_(current_), principal_(std::move(principal)) { current_ = this; }
PrincipalScope::~PrincipalScope() {
    if (current_ != this) std::terminate();
    current_ = previous_;
}
std::shared_ptr<const Principal> PrincipalScope::take() {
    return current_ ? std::exchange(current_->principal_, {}) : nullptr;
}
std::shared_ptr<const Principal> PrincipalScope::peek() {
    return current_ ? current_->principal_ : nullptr;
}
}
