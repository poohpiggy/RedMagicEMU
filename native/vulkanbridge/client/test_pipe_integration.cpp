/* Local integration of EMU pipe client with upstream AndroidPipe services.
 * This test does not initialize a renderer or access a GPU. */
#include "socket_server.h"
#include "../../../tools/diagnostics/gfxstream-principal-scope/principal_scope.h"
#include "../../../tools/diagnostics/gfxstream-vulkanonly-rc/rc_decoder.h"
#include "host-common/opengl/GLProcessPipe.h"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

extern "C" {
int qemu_pipe_open(const char *name);
void qemu_pipe_close(int fd);
int qemu_pipe_write_fully(int fd, const void *buffer, int size);
int qemu_pipe_read_fully(int fd, void *buffer, int size);
}

static bool authorize(pid_t pid, uid_t uid, gid_t, void*) {
    return pid == getpid() && uid == getuid();  // local test only
}

struct TestProcess {
    std::shared_ptr<const rme::Principal> principal = std::make_shared<const rme::Principal>();
    bool drained = false;

    static void drain(void* context) {
        auto& test = *static_cast<TestProcess*>(context);
        unsigned live = 0;
        android::opengl::forEachProcessPipeId([&](uint64_t) { ++live; });
        assert(live == 1 && !test.principal->puid());
        test.drained = true;
    }
};

struct NoReplyStream {
    int writeFully(const void*, size_t) { assert(false); return -1; }
};

int main(int argc, char **argv) {
    assert(argc == 2);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    assert(std::strlen(argv[1]) < sizeof(addr.sun_path));
    std::strcpy(addr.sun_path, argv[1]);
    int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    assert(listener >= 0 && !bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
    assert(!listen(listener, 4));
    rme::Device device;
    assert(device.initialize());
    TestProcess process;
    auto server = rme::SocketServer::create(device, listener, argv[1], authorize, &process,
                                           process.principal, TestProcess::drain);
    assert(server);
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    std::thread missingRenderer([&] {
        int fd = qemu_pipe_open("opengles");
        char reply;
        if (fd >= 0) {
            ok = qemu_pipe_read_fully(fd, &reply, sizeof(reply)) < 0;
            qemu_pipe_close(fd);
        }
        done = true;
    });
    for (int i = 0; i < 250 && !done; ++i) assert(server->runOnce(20));
    assert(done && ok);
    missingRenderer.join();
    done = false;
    ok = false;
    std::atomic<uint64_t> bound{0};
    std::thread client([&] {
        int fd = qemu_pipe_open("GLProcessPipe");
        int32_t confirm = 100;
        uint64_t id = 0;
        if (fd >= 0 && !qemu_pipe_write_fully(fd, &confirm, sizeof(confirm)) &&
            !qemu_pipe_read_fully(fd, &id, sizeof(id)) && id != 0) {
            RcDecoder decoder(process.principal);
            NoReplyStream stream;
            struct { uint32_t opcode; uint32_t size; uint64_t puid; } command{10033, 16, id + 1};
            uint64_t channelPuid = 0;
            assert(decoder.decode(&command, sizeof(command), &stream, &channelPuid) < 0);
            assert(!decoder.acceptsVulkan(channelPuid));
            command.puid = id;
            assert(decoder.decode(&command, sizeof(command), &stream, &channelPuid) == 16);
            assert(decoder.acceptsVulkan(channelPuid));
            bound = channelPuid;
            ok = true;
        }
        if (fd >= 0) qemu_pipe_close(fd);
        done = true;
    });
    bool running = true;
    for (int i = 0; i < 250 && running; ++i) running = server->runOnce(20);
    assert(!running);
    client.join();
    assert(done && ok && bound && process.drained);
    unsigned live = 0;
    android::opengl::forEachProcessPipeId([&](uint64_t) { ++live; });
    assert(live == 0);
    RcDecoder decoder(process.principal);
    assert(!decoder.acceptsVulkan(bound));
    assert(!process.principal->bind(bound));
    std::puts("EMU_CLIENT_REAL_PROCESS_PIPE_BINDING_OK; NO_RENDERER");
}
