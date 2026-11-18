#include "socket_server.h"
#include "host-common/opengl/GLProcessPipe.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <string>

static bool authorize(pid_t pid, uid_t uid, gid_t, void*) { return pid == getpid() && uid == getuid(); }
static unsigned ids() {
    unsigned n = 0;
    android::opengl::forEachProcessPipeId([&](uint64_t) { ++n; });
    return n;
}
int main() {
    char root[] = "/tmp/rme-pipe-server-XXXXXX";
    assert(mkdtemp(root));
    std::string dev = std::string(root) + "/dev";
    std::string dir = dev + "/socket";
    std::string path = dir + "/aemuvk";
    assert(!mkdir(dev.c_str(), 0700) && !mkdir(dir.c_str(), 0700));
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    assert(path.size() < sizeof(addr.sun_path));
    std::strcpy(addr.sun_path, path.c_str());
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    assert(listener >= 0 && !bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) && !listen(listener, 4));
    rme::Device device;
    assert(device.initialize());
    auto server = rme::SocketServer::createForGuest(device, listener, path.c_str(), authorize, nullptr);
    assert(server);
    int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(client >= 0 && !connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
    assert(server->runOnce(100));
    const char prefix[] = "pipe:GLProcessPipe";
    for (size_t i = 0; i < sizeof(prefix); ++i) {
        assert(write(client, prefix + i, 1) == 1 && server->runOnce(100));
    }
    int32_t confirm = 100;
    const char* data = reinterpret_cast<const char*>(&confirm);
    for (size_t i = 0; i < sizeof(confirm); ++i) {
        assert(write(client, data + i, 1) == 1 && server->runOnce(100));
    }
    uint64_t id = 0;
    assert(ids() == 1 && read(client, &id, sizeof(id)) == sizeof(id) && id);
    close(client);
    assert(server->runOnce(100) && ids() == 0);
    int clients[65];
    for (int i = 0; i < 65; ++i) {
        clients[i] = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        assert(clients[i] >= 0 && !connect(clients[i], reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
        assert(server->runOnce(100));
    }
    char byte;
    assert(recv(clients[64], &byte, 1, MSG_DONTWAIT) == 0);
    for (int fd : clients) close(fd);
    server.reset();
    assert(!unlink(path.c_str()) && !rmdir(dir.c_str()) && !rmdir(dev.c_str()) && !rmdir(root));
    std::puts("SOCKET_SERVER_PROCESS_PIPE_OK");
}
