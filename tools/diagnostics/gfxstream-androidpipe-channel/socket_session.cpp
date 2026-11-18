#include "socket_session.h"
#include "shared_memory.h"
#include "../gfxstream-principal-scope/principal_scope.h"
#include "host-common/android_pipe_common.h"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace rme {
namespace {
constexpr size_t kLimit = 65536;
constexpr char kGles[] = "pipe:opengles";
constexpr char kProcess[] = "pipe:GLProcessPipe";
constexpr char kSharedMemory[] = "pipe:RmeVulkanMemory";
bool prefixOf(const std::vector<uint8_t>& bytes, const char* name, size_t n) {
    return bytes.size() <= n && (bytes.empty() || std::memcmp(bytes.data(), name, bytes.size()) == 0);
}
}
struct SocketSession::Impl {
    Device& device;
    int socket;
    std::unique_ptr<Session> pipe;
    std::vector<uint8_t> in, out;
    bool process = false;
    bool confirmed = false;
    bool replied = false;
    bool ownsProcessPipe = false;
    std::shared_ptr<const Principal> principal;
    PeerCredentials peer{};
    Impl(Device& d, int fd) : device(d), socket(fd) {}
    ~Impl() { if (socket >= 0) close(socket); }
};
SocketSession::SocketSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SocketSession::~SocketSession() = default;
std::unique_ptr<SocketSession> SocketSession::create(Device& d, int fd, PeerAuthorization authorize,
                                                       void* opaque, std::shared_ptr<const Principal> principal) {
    auto p = std::make_unique<Impl>(d, fd);
    if (fd < 0 || !authorize || d.wakeupFd() < 0) return {};
    int type = 0;
    socklen_t type_size = sizeof(type);
    struct sockaddr_storage addr{};
    socklen_t addr_size = sizeof(addr);
    struct sockaddr_storage peer{};
    socklen_t peer_size = sizeof(peer);
    struct ucred cred{};
    socklen_t cred_size = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_size) || type != SOCK_STREAM ||
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_size) || addr.ss_family != AF_UNIX ||
        getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &peer_size) || peer.ss_family != AF_UNIX ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_size) || cred_size != sizeof(cred) ||
        !authorize(cred.pid, cred.uid, cred.gid, opaque)) return {};
    p->peer = {cred.pid, cred.uid, cred.gid};
    p->principal = std::move(principal);
    return std::unique_ptr<SocketSession>(new SocketSession(std::move(p)));
}
PeerCredentials SocketSession::peer() const { return impl_->peer; }
void SocketSession::bindPrincipal(std::shared_ptr<const Principal> principal) {
    impl_->principal = std::move(principal);
}
bool SocketSession::ownsProcessPipe() const { return impl_->ownsProcessPipe; }
int SocketSession::fd() const { return impl_->socket; }
short SocketSession::events() const {
    return (impl_->in.size() < kLimit ? POLLIN : 0) | (!impl_->out.empty() ? POLLOUT : 0);
}
bool SocketSession::needsPump() {
    auto& s = *impl_;
    if (!s.pipe) return false;
    if (s.process) return !s.confirmed && s.in.size() >= sizeof(int32_t);
    unsigned state = s.pipe->poll();
    return (state & PIPE_POLL_HUP) || (!s.in.empty() && (state & PIPE_POLL_OUT)) ||
           (s.out.size() < kLimit && (state & PIPE_POLL_IN));
}
bool SocketSession::pump() {
    auto& s = *impl_;
    if (s.in.size() < kLimit) {
        uint8_t buf[4096];
        ssize_t n;
        do { n = recv(s.socket, buf, std::min(sizeof(buf), kLimit - s.in.size()), MSG_DONTWAIT); }
        while (n < 0 && errno == EINTR);
        if (n == 0) return false;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (n > 0) s.in.insert(s.in.end(), buf, buf + n);
    }
    if (!s.pipe) {
        auto nul = std::find(s.in.begin(), s.in.end(), 0);
        if (nul == s.in.end()) {
            if (!prefixOf(s.in, kGles, sizeof(kGles)) &&
                !prefixOf(s.in, kProcess, sizeof(kProcess)) &&
                !prefixOf(s.in, kSharedMemory, sizeof(kSharedMemory))) return false;
            return true;
        }
        size_t n = static_cast<size_t>(nul - s.in.begin()) + 1;
        if (n == sizeof(kSharedMemory) && !std::memcmp(s.in.data(), kSharedMemory, n)) {
            if (s.in.size() < n + sizeof(uint64_t)) return true;
            const uint64_t owner = s.principal ? s.principal->puid() : 0;
            if (!owner || s.in.size() != n + sizeof(uint64_t)) return false;
            uint64_t memory;
            std::memcpy(&memory, s.in.data() + n, sizeof(memory));
            bool cached = false;
            const int fd = duplicateSharedMemory(owner, memory, &cached);
            sendSharedMemory(s.socket, fd, cached);
            if (fd >= 0) close(fd);
            return false;
        }
        if (n == sizeof(kGles) && !std::memcmp(s.in.data(), kGles, n)) s.process = false;
        else if (n == sizeof(kProcess) && !std::memcmp(s.in.data(), kProcess, n)) s.process = true;
        else return false;
        if (s.process) {
            s.pipe = s.device.open(Service::Process);
        } else {
            PrincipalScope scope(s.principal);
            s.pipe = s.device.open(Service::OpenGLES);
        }
        if (!s.pipe) return false;
        s.in.erase(s.in.begin(), s.in.begin() + n);
    }
    if (s.process) {
        if (!s.confirmed && s.in.size() >= sizeof(int32_t)) {
            if (s.pipe->send(s.in.data(), sizeof(int32_t)) != sizeof(int32_t)) return false;
            s.in.erase(s.in.begin(), s.in.begin() + sizeof(int32_t));
            s.confirmed = true;
        }
        if (s.confirmed && !s.in.empty()) return false;
        if (s.confirmed && !s.replied) {
            uint64_t id;
            if (s.pipe->receive(&id, sizeof(id)) != sizeof(id)) return false;
            if (s.principal) {
                if (!s.principal->bind(id)) return false;
                s.ownsProcessPipe = true;
            }
            const auto* p = reinterpret_cast<const uint8_t*>(&id);
            s.out.insert(s.out.end(), p, p + sizeof(id));
            s.replied = true;
        }
    } else {
        unsigned state = s.pipe->poll();
        if (state & PIPE_POLL_HUP) return false;
        unsigned watch = 0;
        if (!s.in.empty()) {
            if (state & PIPE_POLL_OUT) {
                int n = s.pipe->send(s.in.data(), s.in.size());
                if (n < 0 && n != PIPE_ERROR_AGAIN) return false;
                if (n > 0) s.in.erase(s.in.begin(), s.in.begin() + n);
            }
            if (!s.in.empty()) watch |= PIPE_WAKE_WRITE;
        }
        if (s.out.size() < kLimit) {
            if (state & PIPE_POLL_IN) {
                uint8_t buf[4096];
                int n = s.pipe->receive(buf, std::min(sizeof(buf), kLimit - s.out.size()));
                if (n <= 0 && n != PIPE_ERROR_AGAIN) return false;
                if (n > 0) s.out.insert(s.out.end(), buf, buf + n);
            } else watch |= PIPE_WAKE_READ;
        }
        if (watch && !s.pipe->watch(watch)) return false;
    }
    if (!s.out.empty()) {
        ssize_t n;
        do { n = send(s.socket, s.out.data(), s.out.size(), MSG_DONTWAIT | MSG_NOSIGNAL); }
        while (n < 0 && errno == EINTR);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (n > 0) s.out.erase(s.out.begin(), s.out.begin() + n);
    }
    return true;
}
}
