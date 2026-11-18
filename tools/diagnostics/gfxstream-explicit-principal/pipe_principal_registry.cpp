#include "pipe_principal_registry.h"
#include <mutex>
#include <unordered_map>
#include <utility>

namespace rme {
struct PipePrincipalRegistry::Entry { std::shared_ptr<const PipePrincipal> principal; };
struct PipePrincipalRegistry::State {
    std::mutex mutex;
    std::unordered_map<const void*, std::weak_ptr<Entry>> entries;
};
PipePrincipalRegistry::Binding::Binding(std::shared_ptr<State> state, const void* key,
                                         std::shared_ptr<Entry> entry)
    : state_(std::move(state)), key_(key), entry_(std::move(entry)) {}
PipePrincipalRegistry::Binding::~Binding() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto it = state_->entries.find(key_);
    if (it != state_->entries.end() && it->second.lock() == entry_) state_->entries.erase(it);
}
PipePrincipalRegistry::PipePrincipalRegistry() : state_(std::make_shared<State>()) {}
std::unique_ptr<PipePrincipalRegistry::Binding> PipePrincipalRegistry::bind(
        const void* hwpipe, std::shared_ptr<const PipePrincipal> principal) {
    if (!hwpipe || !principal) return {};
    auto entry = std::make_shared<Entry>();
    entry->principal = std::move(principal);
    auto binding = std::unique_ptr<Binding>(new Binding(state_, hwpipe, entry));
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->entries.find(hwpipe);
        if (it != state_->entries.end() && !it->second.expired()) return {};
        state_->entries[hwpipe] = entry;
    }
    return binding;
}
std::shared_ptr<const PipePrincipal> PipePrincipalRegistry::lookup(const void* hwpipe) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto it = state_->entries.find(hwpipe);
    if (it == state_->entries.end()) return {};
    auto entry = it->second.lock();
    return entry ? entry->principal : nullptr;
}
}
