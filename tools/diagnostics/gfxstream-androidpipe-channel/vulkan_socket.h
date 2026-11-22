#pragma once

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace rme {
enum class VulkanTransport { Socket, ReplyPipe, DuplexPipe };

class VulkanSocket final {
public:
    VulkanSocket() = default;
    VulkanSocket(const VulkanSocket&) = delete;
    VulkanSocket& operator=(const VulkanSocket&) = delete;
    ~VulkanSocket() {
        if (request_ >= 0) close(request_);
        if (requestWake_ >= 0) close(requestWake_);
        if (reply_ >= 0) close(reply_);
        if (fd_ >= 0) close(fd_);
    }

    static std::shared_ptr<VulkanSocket> open(int fd, const std::vector<uint8_t>& input,
                                             size_t selectorSize,
                                             VulkanTransport transport = VulkanTransport::Socket) {
        auto stream = std::make_shared<VulkanSocket>();
        stream->prefetched_.assign(input.begin() + selectorSize, input.end());
        stream->fd_ = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (stream->fd_ < 0) return {};
        // The server stops reading this socket as soon as the worker takes it.
        const int flags = fcntl(stream->fd_, F_GETFL);
        if (flags < 0 || fcntl(stream->fd_, F_SETFL, flags & ~O_NONBLOCK) < 0) return {};
        if (transport != VulkanTransport::Socket) {
            if (!stream->prefetched_.empty() || !stream->openPipes(transport)) return {};
            stream->usesReply_ = true;
        }
        return stream;
    }

    // The server may interrupt a worker blocked in the socket during teardown.
    void stop() {
        if (stopping_.exchange(true, std::memory_order_acq_rel)) return;
        shutdown(fd_, SHUT_RDWR);
        if (requestWake_ >= 0) {
            // Wake a blocked request reader. It checks stopping_ before
            // interpreting a byte. If the pipe is full, a read already wakes.
            const uint8_t wake = 0;
            ssize_t result;
            do { result = ::write(requestWake_, &wake, 1); } while (result < 0 && errno == EINTR);
        }
        if (usesReply_) {
            std::lock_guard<std::mutex> lock(replyLock_);
            if (reply_ >= 0) close(std::exchange(reply_, -1));
        }
    }

    ssize_t read(void* output, size_t size) {
        if (request_ >= 0) {
            ssize_t count;
            if (stopping_.load(std::memory_order_acquire)) return 0;
            do { count = ::read(request_, output, size); } while (count < 0 && errno == EINTR);
            return stopping_.load(std::memory_order_acquire) ? 0 : count;
        }
        const auto available = prefetched_.size() - consumed_;
        if (available) {
            const auto count = std::min(size, available);
            std::memcpy(output, prefetched_.data() + consumed_, count);
            consumed_ += count;
            return count;
        }
        ssize_t count;
        do { count = recv(fd_, output, size, 0); } while (count < 0 && errno == EINTR);
        return count;
    }

    bool write(const void* input, size_t size) {
        if (usesReply_) return writeReply(input, size);
        const auto* bytes = static_cast<const uint8_t*>(input);
        while (size) {
            ssize_t count;
            do { count = send(fd_, bytes, size, MSG_NOSIGNAL); } while (count < 0 && errno == EINTR);
            if (count <= 0) return false;
            bytes += count;
            size -= count;
        }
        return true;
    }

private:
    bool openPipes(VulkanTransport transport) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) < 0) return false;
        reply_ = fds[1];
        int passed[] = {fds[0], -1, -1};
        size_t count = 1;
        char marker = 'P';
        if (transport == VulkanTransport::DuplexPipe) {
            int request[2];
            if (pipe2(request, O_CLOEXEC) < 0) {
                close(fds[0]);
                return false;
            }
            request_ = request[0];
            requestWake_ = request[1];
            if (!setNonBlocking(requestWake_)) {
                close(fds[0]);
                return false;
            }
            // The client keeps an unused read endpoint for the command pipe.
            // A disconnected host cannot cause SIGPIPE in the Vulkan caller.
            passed[1] = requestWake_;
            passed[2] = request_;
            count = 3;
            marker = 'D';
        }
        iovec data{&marker, 1};
        union { cmsghdr aligned; char bytes[CMSG_SPACE(sizeof(passed))]; } control{};
        msghdr message{};
        message.msg_iov = &data;
        message.msg_iovlen = 1;
        message.msg_control = control.bytes;
        message.msg_controllen = CMSG_SPACE(count * sizeof(int));
        auto* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(count * sizeof(int));
        std::memcpy(CMSG_DATA(header), passed, count * sizeof(int));
        const bool sent = setNonBlocking(reply_) &&
                          sendmsg(fd_, &message, MSG_DONTWAIT | MSG_NOSIGNAL) == 1;
        close(fds[0]);
        return sent;
    }

    static bool setNonBlocking(int fd) {
        const int flags = fcntl(fd, F_GETFL);
        return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    bool writeReply(const void* input, size_t size) {
        std::lock_guard<std::mutex> lock(replyLock_);
        if (reply_ < 0) return false;
        auto* data = static_cast<const uint8_t*>(input);
        while (size) {
            // The renderer service ignores SIGPIPE. A full pipe must still
            // let the control socket cancel the worker during shutdown.
            const ssize_t count = ::write(reply_, data, size);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                pollfd ready[] = {{reply_, POLLOUT, 0}, {fd_, 0, 0}};
                int result;
                do { result = poll(ready, 2, -1); } while (result < 0 && errno == EINTR);
                if (result > 0 && !ready[1].revents && (ready[0].revents & POLLOUT)) continue;
                return false;
            }
            if (count <= 0) return false;
            data += count;
            size -= count;
        }
        return true;
    }

    int fd_ = -1;
    int reply_ = -1;
    int request_ = -1;
    int requestWake_ = -1;
    std::atomic<bool> stopping_{false};
    bool usesReply_ = false;
    std::mutex replyLock_;
    std::vector<uint8_t> prefetched_;
    size_t consumed_ = 0;
};

// The RenderThread constructor runs on the server thread while the pipe opens.
// A constructor that supports the socket can take it once during that call.
class VulkanSocketScope final {
public:
    VulkanSocketScope(int fd, const std::vector<uint8_t>& input, size_t selectorSize,
                      VulkanTransport transport = VulkanTransport::Socket)
        : previous_(std::exchange(current_, this)), fd_(fd), input_(input),
          selectorSize_(selectorSize), transport_(transport) {}
    VulkanSocketScope(const VulkanSocketScope&) = delete;
    VulkanSocketScope& operator=(const VulkanSocketScope&) = delete;
    ~VulkanSocketScope() { current_ = previous_; }

    static std::shared_ptr<VulkanSocket> take() {
        auto* scope = current_;
        if (!scope || scope->fd_ < 0) return {};
        return scope->socket_ = VulkanSocket::open(std::exchange(scope->fd_, -1),
                                                  scope->input_, scope->selectorSize_, scope->transport_);
    }
    const std::shared_ptr<VulkanSocket>& socket() const { return socket_; }

private:
    static inline thread_local VulkanSocketScope* current_ = nullptr;
    VulkanSocketScope* previous_;
    int fd_;
    const std::vector<uint8_t>& input_;
    size_t selectorSize_;
    VulkanTransport transport_;
    std::shared_ptr<VulkanSocket> socket_;
};
}
