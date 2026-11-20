#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace rme {
class VulkanSocket final {
public:
    VulkanSocket() = default;
    VulkanSocket(const VulkanSocket&) = delete;
    VulkanSocket& operator=(const VulkanSocket&) = delete;
    ~VulkanSocket() { if (fd_ >= 0) close(fd_); }

    static std::shared_ptr<VulkanSocket> open(int fd, const std::vector<uint8_t>& input,
                                             size_t selectorSize) {
        auto stream = std::make_shared<VulkanSocket>();
        stream->prefetched_.assign(input.begin() + selectorSize, input.end());
        stream->fd_ = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (stream->fd_ < 0) return {};
        // The server stops reading this socket as soon as the worker takes it.
        const int flags = fcntl(stream->fd_, F_GETFL);
        if (flags < 0 || fcntl(stream->fd_, F_SETFL, flags & ~O_NONBLOCK) < 0) return {};
        return stream;
    }

    // The server may interrupt a worker blocked in the socket during teardown.
    void stop() const { shutdown(fd_, SHUT_RDWR); }

    ssize_t read(void* output, size_t size) {
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
    int fd_ = -1;
    std::vector<uint8_t> prefetched_;
    size_t consumed_ = 0;
};

// The RenderThread constructor runs on the server thread while the pipe opens.
// A constructor that supports the socket can take it once during that call.
class VulkanSocketScope final {
public:
    VulkanSocketScope(int fd, const std::vector<uint8_t>& input, size_t selectorSize)
        : previous_(std::exchange(current_, this)), fd_(fd), input_(input),
          selectorSize_(selectorSize) {}
    VulkanSocketScope(const VulkanSocketScope&) = delete;
    VulkanSocketScope& operator=(const VulkanSocketScope&) = delete;
    ~VulkanSocketScope() { current_ = previous_; }

    static std::shared_ptr<VulkanSocket> take() {
        auto* scope = current_;
        if (!scope || scope->fd_ < 0) return {};
        return scope->socket_ = VulkanSocket::open(std::exchange(scope->fd_, -1),
                                                  scope->input_, scope->selectorSize_);
    }
    const std::shared_ptr<VulkanSocket>& socket() const { return socket_; }

private:
    static inline thread_local VulkanSocketScope* current_ = nullptr;
    VulkanSocketScope* previous_;
    int fd_;
    const std::vector<uint8_t>& input_;
    size_t selectorSize_;
    std::shared_ptr<VulkanSocket> socket_;
};
}
