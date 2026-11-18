#include "socket_session.h"
#include "host-common/opengl/GLProcessPipe.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

static bool authorize(pid_t pid, uid_t uid, gid_t, void*) { return pid == getpid() && uid == getuid(); }
static bool deny(pid_t, uid_t, gid_t, void*) { return false; }
static unsigned count_ids() {
    unsigned count = 0;
    android::opengl::forEachProcessPipeId([&](uint64_t) { ++count; });
    return count;
}
int main() {
    rme::Device device;
    assert(device.initialize());
    int fd[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    assert(!rme::SocketSession::create(device, fd[0], deny, nullptr));
    close(fd[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    auto session = rme::SocketSession::create(device, fd[0], authorize, nullptr);
    assert(session);
    const auto peer = session->peer();
    assert(peer.pid == getpid() && peer.uid == getuid() && peer.gid == getgid());
    const char prefix[] = "pipe:GLProcessPipe";
    for (size_t i = 0; i < sizeof(prefix); ++i) {
        assert(write(fd[1], &prefix[i], 1) == 1);
        assert(session->pump());
    }
    int32_t confirm = 100;
    const auto* bytes = reinterpret_cast<const char*>(&confirm);
    for (size_t i = 0; i < sizeof(confirm); ++i) {
        assert(write(fd[1], bytes + i, 1) == 1);
        assert(session->pump());
    }
    assert(count_ids() == 1);
    uint64_t id = 0;
    assert(read(fd[1], &id, sizeof(id)) == sizeof(id) && id);
    close(fd[1]);
    assert(!session->pump());
    session.reset();
    assert(count_ids() == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    session = rme::SocketSession::create(device, fd[0], authorize, nullptr);
    assert(session);
    const char invalid[] = "pipe:invalid";
    assert(write(fd[1], invalid, sizeof(invalid)) == sizeof(invalid));
    assert(!session->pump());
    session.reset();
    close(fd[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    session = rme::SocketSession::create(device, fd[0], authorize, nullptr);
    assert(session);
    const char opengles[] = "pipe:opengles";
    assert(write(fd[1], opengles, sizeof(opengles)) == sizeof(opengles));
    assert(!session->pump()); // No renderer was initialized.
    session.reset();
    close(fd[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
    session = rme::SocketSession::create(device, fd[0], authorize, nullptr);
    assert(session);
    assert(write(fd[1], prefix, sizeof(prefix)) == sizeof(prefix) && session->pump());
    int32_t bad = 99;
    assert(write(fd[1], &bad, sizeof(bad)) == sizeof(bad) && !session->pump());
    session.reset();
    close(fd[1]);
    assert(count_ids() == 0);
}
