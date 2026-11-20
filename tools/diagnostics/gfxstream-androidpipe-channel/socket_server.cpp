#include "socket_server.h"
#include "../gfxstream-principal-scope/principal_scope.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace rme {
struct SocketServer::Impl {
    struct Peer {
        int pidfd = -1;
        bool retiring = false;
        const std::shared_ptr<const Principal> principal = std::make_shared<const Principal>();
        ~Peer() { if (pidfd >= 0) close(pidfd); }

        bool live() const {
            pollfd fd{pidfd, POLLIN, 0};
            int n;
            do { n = poll(&fd, 1, 0); } while (n < 0 && errno == EINTR);
            return pidfd >= 0 && n == 0;
        }
    };
    Device& device;
    int listener;
    PeerAuthorization authorize;
    void* opaque;
    std::shared_ptr<const Principal> principal;
    SocketServer::Drain drain;
    std::vector<std::unique_ptr<SocketSession>> clients;
    std::vector<pollfd> pollFds;
    bool perProcess = false;
    std::map<pid_t, Peer> peers;
    Impl(Device& d, int fd, PeerAuthorization a, void* o,
         std::shared_ptr<const Principal> p, SocketServer::Drain callback)
        : device(d), listener(fd), authorize(a), opaque(o), principal(std::move(p)), drain(callback) {}
    ~Impl() { shutdown(); }

    void retire(pid_t pid, Peer& peer) {
        peer.principal->revoke();
        std::unique_ptr<SocketSession> processPipe;
        clients.erase(std::remove_if(clients.begin(), clients.end(), [&](auto& client) {
            if (client->peer().pid != pid) return false;
            if (client->ownsProcessPipe()) processPipe = std::move(client);
            return true;
        }), clients.end());
        peer.principal->drainWorkers();
    }

    void accept(int fd) {
        const auto check = perProcess ? +[](pid_t pid, uid_t uid, gid_t, void*) {
            return pid > 1 && uid == getuid();
        } : authorize;
        auto client = SocketSession::create(device, fd, check, opaque, principal);
        if (!client) return;
        if (perProcess) {
            const auto credentials = client->peer();
            auto [entry, fresh] = peers.try_emplace(credentials.pid);
            auto& peer = entry->second;
            if (fresh) peer.pidfd = syscall(SYS_pidfd_open, credentials.pid, 0);
            if (!peer.live() || (fresh && (!authorize(credentials.pid, credentials.uid, credentials.gid, opaque) ||
                                           !peer.live()))) {
                if (fresh) peers.erase(entry);
                return;
            }
            client->bindPrincipal(peer.principal);
        }
        clients.push_back(std::move(client));
    }

    void shutdown() {
        if (listener < 0) return;
        close(listener);
        listener = -1;
        for (auto& [pid, peer] : peers) retire(pid, peer);
        peers.clear();
        if (principal) principal->revoke();
        std::unique_ptr<SocketSession> processPipe;
        for (auto& client : clients) {
            if (client->ownsProcessPipe()) processPipe = std::move(client);
        }
        clients.clear();
        if (processPipe) drain(opaque);
    }
};
SocketServer::SocketServer(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
SocketServer::~SocketServer() = default;
std::unique_ptr<SocketServer> SocketServer::create(Device& device, int fd, const char* path,
                                                   PeerAuthorization authorize, void* opaque,
                                                   std::shared_ptr<const Principal> principal, Drain drain) {
    auto p = std::make_unique<Impl>(device, fd, authorize, opaque, std::move(principal), drain);
    if (static_cast<bool>(p->principal) != static_cast<bool>(drain)) return {};
    if (fd < 0 || !path || path[0] != '/' || !authorize || device.wakeupFd() < 0) return {};
    constexpr char suffix[] = "/dev/socket/aemuvk";
    size_t len = std::strlen(path);
    if (len <= sizeof(suffix) - 1 || std::strcmp(path + len - (sizeof(suffix) - 1), suffix)) return {};
    struct sockaddr_un addr{};
    socklen_t size = sizeof(addr);
    int accept_conn = 0;
    socklen_t accept_size = sizeof(accept_conn);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &size) || addr.sun_family != AF_UNIX ||
        size <= offsetof(sockaddr_un, sun_path) || addr.sun_path[0] == 0 ||
        !std::memchr(addr.sun_path, 0, sizeof(addr.sun_path)) || std::strcmp(addr.sun_path, path) ||
        getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &accept_conn, &accept_size) || !accept_conn) return {};
    return std::unique_ptr<SocketServer>(new SocketServer(std::move(p)));
}
std::unique_ptr<SocketServer> SocketServer::createForGuest(Device& device, int listener, const char* path,
                                                          PeerAuthorization authorize, void* opaque) {
    auto server = create(device, listener, path, authorize, opaque);
    if (server) server->impl_->perProcess = true;
    return server;
}

bool SocketServer::runOnce(int timeout_ms) {
    auto& s = *impl_;
    if (timeout_ms < -1 || s.listener < 0 || s.device.wakeupFd() < 0) return false;
    bool immediate = false;
    auto& fds = s.pollFds;
    fds.clear();
    fds.push_back({s.listener, POLLIN, 0});
    fds.push_back({s.device.wakeupFd(), POLLIN, 0});
    for (auto& client : s.clients) {
        fds.push_back({client->fd(), client->events(), 0});
        immediate = client->needsPump() || immediate;
    }
    size_t peerFd = fds.size();
    for (const auto& entry : s.peers) {
        fds.push_back({entry.second.pidfd, POLLIN, 0});
    }
    int n;
    do { n = poll(fds.data(), fds.size(), immediate ? 0 : timeout_ms); } while (n < 0 && errno == EINTR);
    if (n < 0 || (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
        (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) return false;
    if ((fds[1].revents & POLLIN) && !s.device.dispatchWakeups()) return false;
    for (size_t i = s.clients.size(); i > 0; --i) {
        size_t index = i - 1;
        auto& client = s.clients[index];
        const auto events = fds[index + 2].revents;
        if ((events || fds[1].revents || client->needsPump()) && !client->pump(events)) {
            if (client->ownsProcessPipe()) {
                if (!s.perProcess) {
                    s.shutdown();
                    return false;
                }
                s.peers.at(client->peer().pid).retiring = true;
            } else {
                s.clients.erase(s.clients.begin() + index);
            }
        }
    }
    for (auto it = s.peers.begin(); it != s.peers.end();) {
        const auto pid = it->first;
        auto& peer = it->second;
        const bool exited = fds[peerFd++].revents != 0;
        if (peer.retiring || exited || std::none_of(s.clients.begin(), s.clients.end(),
            [pid](const auto& client) { return client->peer().pid == pid; })) {
            s.retire(pid, peer);
            it = s.peers.erase(it);
        } else {
            ++it;
        }
    }
    if (fds[0].revents & POLLIN) {
        int fd;
        do { fd = accept4(s.listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK); }
        while (fd < 0 && errno == EINTR);
        if (fd < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (fd >= 0) {
            if (s.clients.size() >= 64) {
                close(fd);
            } else {
                s.accept(fd);
            }
        }
    }
    return true;
}
}
