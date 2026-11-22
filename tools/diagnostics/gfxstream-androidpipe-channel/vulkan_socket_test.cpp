#include "vulkan_socket.h"
#include <cassert>
#include <cstdio>
#include <poll.h>
#include <thread>

static void receivePipeFds(int socket, char expected, int* fds, size_t count) {
    char marker = 0;
    iovec data{&marker, 1};
    union { cmsghdr aligned; char bytes[CMSG_SPACE(3 * sizeof(int))]; } control{};
    msghdr message{};
    message.msg_iov = &data;
    message.msg_iovlen = 1;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    assert(recvmsg(socket, &message, MSG_CMSG_CLOEXEC) == 1 && marker == expected);
    auto* header = CMSG_FIRSTHDR(&message);
    assert(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
           header->cmsg_len == CMSG_LEN(count * sizeof(int)));
    std::memcpy(fds, CMSG_DATA(header), count * sizeof(int));
}

int main() {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, fds) == 0);
    const std::vector<uint8_t> input{'s', 0, 2, 3};
    assert(!rme::VulkanSocketScope::take());
    std::shared_ptr<rme::VulkanSocket> socket;
    {
        rme::VulkanSocketScope scope(fds[0], input, 2);
        rme::VulkanSocketScope disabled(-1, input, 0);
        assert(!rme::VulkanSocketScope::take());
    }
    {
        rme::VulkanSocketScope scope(fds[0], input, 2);
        socket = rme::VulkanSocketScope::take();
        assert(socket && scope.socket() == socket);
        assert(!rme::VulkanSocketScope::take());
    }
    const uint8_t request = 4;
    assert(send(fds[1], &request, 1, MSG_NOSIGNAL) == 1);
    std::thread worker([&] {
        uint8_t bytes[3]{};
        assert(socket->read(bytes, sizeof(bytes)) == 2);
        assert(socket->read(bytes + 2, 1) == 1);
        assert(bytes[0] == 2 && bytes[1] == 3 && bytes[2] == request);
        assert(socket->write(bytes, sizeof(bytes)));
        assert(socket->read(bytes, sizeof(bytes)) == 0);
    });
    pollfd client{fds[1], POLLIN, 0};
    assert(poll(&client, 1, 1000) == 1 && (client.revents & POLLIN));
    uint8_t reply[3]{};
    assert(recv(fds[1], reply, sizeof(reply), 0) == sizeof(reply));
    assert(reply[0] == 2 && reply[1] == 3 && reply[2] == request);
    socket->stop();
    worker.join();
    pollfd server{fds[0], 0, 0};
    assert(poll(&server, 1, 1000) == 1 && (server.revents & POLLHUP));
    assert(!socket->write(&request, 1));
    close(fds[0]);
    close(fds[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);
    socket = rme::VulkanSocket::open(fds[0], {'s', 0}, 2, rme::VulkanTransport::ReplyPipe);
    assert(socket);
    int replyPipe;
    receivePipeFds(fds[1], 'P', &replyPipe, 1);
    const uint8_t value = 83;
    assert(socket->write(&value, 1));
    uint8_t seen = 0;
    assert(read(replyPipe, &seen, 1) == 1 && seen == value);
    std::thread largeReply([&] {
        const std::vector<uint8_t> large(8 * 1024 * 1024, value);
        assert(!socket->write(large.data(), large.size()));
    });
    pollfd buffered{replyPipe, POLLIN, 0};
    assert(poll(&buffered, 1, 1000) == 1 && (buffered.revents & POLLIN));
    socket->stop();
    largeReply.join();
    char discard[4096];
    ssize_t count;
    do { count = read(replyPipe, discard, sizeof(discard)); } while (count > 0);
    assert(count == 0);
    close(replyPipe);
    close(fds[0]);
    close(fds[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0);
    socket = rme::VulkanSocket::open(fds[0], {'s', 0}, 2, rme::VulkanTransport::DuplexPipe);
    assert(socket);
    int pipeFds[3];
    receivePipeFds(fds[1], 'D', pipeFds, 3);
    assert(write(pipeFds[1], &value, 1) == 1);
    seen = 0;
    assert(socket->read(&seen, 1) == 1 && seen == value);
    std::thread pendingRequest([&] { assert(socket->read(&seen, 1) == 0); });
    socket->stop();
    pendingRequest.join();
    assert(read(pipeFds[0], &seen, 1) == 0);
    for (const int fd : pipeFds) close(fd);
    close(fds[0]);
    close(fds[1]);
    std::puts("VULKAN_SOCKET_HANDOFF_AND_SHUTDOWN_OK");
}
