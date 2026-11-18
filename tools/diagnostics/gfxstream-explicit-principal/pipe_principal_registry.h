#pragma once
#include <memory>

namespace rme {
// Identity is supplied by trusted host code. This type does not authenticate.
struct PipePrincipal final {
    PipePrincipal() = default;
    PipePrincipal(const PipePrincipal&) = delete;
    PipePrincipal& operator=(const PipePrincipal&) = delete;
};

// Maps a host-owned hwPipe address to a lifetime-held principal. No Guest data
// may be used as the address. A lookup result only guarantees object lifetime;
// it is not a live-connection authorization lease after unbind.
class PipePrincipalRegistry final {
    struct State;
    struct Entry;
public:
    class Binding final {
    public:
        ~Binding();
        Binding(const Binding&) = delete;
        Binding& operator=(const Binding&) = delete;
    private:
        friend class PipePrincipalRegistry;
        Binding(std::shared_ptr<State>, const void*, std::shared_ptr<Entry>);
        std::shared_ptr<State> state_;
        const void* key_;
        std::shared_ptr<Entry> entry_;
    };
    PipePrincipalRegistry();
    std::unique_ptr<Binding> bind(const void* hwpipe, std::shared_ptr<const PipePrincipal>);
    std::shared_ptr<const PipePrincipal> lookup(const void* hwpipe) const;
private:
    std::shared_ptr<State> state_;
};
}
