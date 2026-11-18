#pragma once
#include "socket_session.h"
#include <memory>

namespace rme {
class SocketServer {
public:
    using Drain = void (*)(void*);
    // Takes ownership of a listening fd on both success and failure. The
    // caller creates and isolates the per-instance socket, and authorizes peers.
    // A bound server accepts exactly one authenticated process. The caller
    // must prove that every admitted socket belongs to that same live process.
    // Drain stops and joins graphics workers while the process pipe is alive.
    static std::unique_ptr<SocketServer> create(Device&, int listener,
                                                const char* expected_path,
                                                PeerAuthorization, void*,
                                                std::shared_ptr<const Principal> = {},
                                                Drain = nullptr);
    static std::unique_ptr<SocketServer> createForGuest(Device&, int listener,
                                                       const char* expected_path,
                                                       PeerAuthorization, void*);
    ~SocketServer();
    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;
    // Poll socket and AndroidPipe wakeups once, on the device thread.
    bool runOnce(int timeout_ms);
private:
    struct Impl;
    explicit SocketServer(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
}
