#pragma once
#include "channel.h"
#include <memory>
#include <sys/types.h>

namespace rme {
struct Principal;
// Caller must implement per-instance authorization, not merely compare UIDs.
using PeerAuthorization = bool (*)(pid_t, uid_t, gid_t, void*);
struct PeerCredentials {
    pid_t pid;
    uid_t uid;
    gid_t gid;
};
class SocketSession {
public:
    // Takes ownership of fd whether authorization succeeds or fails.
    static std::unique_ptr<SocketSession> create(Device&, int fd, PeerAuthorization, void*,
                                                  std::shared_ptr<const Principal> = {});
    ~SocketSession();
    SocketSession(const SocketSession&) = delete;
    SocketSession& operator=(const SocketSession&) = delete;
    // Nonblocking; false means close this connection. Device thread only.
    bool pump();
    // Kernel credentials captured at successful authorization for this live
    // connection only. They are not a per-instance proof or a durable process ID.
    PeerCredentials peer() const;
    void bindPrincipal(std::shared_ptr<const Principal>);
    int fd() const;
    short events() const;
    bool needsPump();
    bool ownsProcessPipe() const;
private:
    struct Impl;
    explicit SocketSession(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
}
