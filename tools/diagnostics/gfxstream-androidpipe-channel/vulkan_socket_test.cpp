#include "vulkan_socket.h"
#include <cassert>
#include <cstdio>
#include <poll.h>
#include <thread>

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
    std::puts("VULKAN_SOCKET_HANDOFF_AND_SHUTDOWN_OK");
}
