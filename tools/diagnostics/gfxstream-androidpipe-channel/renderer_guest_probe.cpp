#include "renderer_guest_probe.h"
#include "socket_server.h"
#include "../gfxstream-principal-scope/principal_scope.h"
#include "../../../native/guest_process_identity.h"
#include "host-common/opengles.h"

#include <cerrno>
#include <chrono>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
struct ProbeChild {
    pid_t pid = -1;
    static bool authorize(pid_t pid, uid_t uid, gid_t, void* context) {
        return pid == static_cast<ProbeChild*>(context)->pid && uid == getuid();
    }
    static void drain(void*) { android_finishOpenglesRenderer(); }
};

int fail(const char* step, int detail) {
    std::fprintf(stderr, "RME_GUEST_VULKAN_PROBE_FAILED %s detail=%d\n", step, detail);
    return 1;
}
struct SocketPath {
    const std::string name;
    bool bound = false;
    ~SocketPath() { if (bound) unlink(name.c_str()); }
};

int listenForGuest(SocketPath& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.name.size() >= sizeof(address.sun_path)) return -ENAMETOOLONG;
    std::memcpy(address.sun_path, path.name.c_str(), path.name.size() + 1);
    const int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listener < 0) return -errno;
    const auto mask = umask(0077);
    const int bound = bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const int bindError = errno;
    umask(mask);
    if (bound != 0) {
        close(listener);
        return -bindError;
    }
    path.bound = true;
    if (listen(listener, 4) != 0) {
        const int error = errno;
        close(listener);
        return -error;
    }
    return listener;
}

struct AttachedProcess {
    pid_t pid = -1;
    int fd = -1;
    ~AttachedProcess() { if (fd >= 0) close(fd); }

    bool pin(pid_t target) {
        pid = target;
        fd = syscall(SYS_pidfd_open, pid, 0);
        return fd >= 0 && live();
    }

    bool live() const {
        pollfd death{fd, POLLIN, 0};
        int count;
        do { count = poll(&death, 1, 0); } while (count < 0 && errno == EINTR);
        return count == 0;
    }

    static bool authorize(pid_t pid, uid_t uid, gid_t, void* context) {
        const auto& process = *static_cast<AttachedProcess*>(context);
        return pid == process.pid && uid == getuid() && process.live();
    }
};

pid_t readTarget() {
    pollfd input{STDIN_FILENO, POLLIN, 0};
    constexpr int timeoutMs = 20000;
    char line[16];
    for (size_t size = 0; size < sizeof(line); ++size) {
        if (poll(&input, 1, timeoutMs) != 1 || read(STDIN_FILENO, line + size, 1) != 1) return -1;
        if (line[size] == '\n') {
            pid_t pid = -1;
            const auto result = std::from_chars(line, line + size, pid);
            return result.ec == std::errc{} && result.ptr == line + size && pid > 1 ? pid : -1;
        }
    }
    return -1;
}

}

int rme::runGuestDeviceProbe(const char* root, const char* runner, const char* program,
                             const char* guestLibraryDir, const char* probeMode) {
    Device device;
    if (!device.initialize()) return fail("AndroidPipe initialization", 0);
    SocketPath path{std::string(root) + "/dev/socket/aemuvk"};
    const int listener = listenForGuest(path);
    if (listener < 0) return fail("listen", -listener);
    ProbeChild child;
    const auto principal = std::make_shared<const Principal>();
    auto server = SocketServer::create(device, listener, path.name.c_str(), ProbeChild::authorize,
                                       &child, principal, ProbeChild::drain);
    if (!server) return fail("AndroidPipe socket server", 0);

    // Same native runner and path redirection as EMU's GuestRunner.
    std::string rootEnv = std::string("AEMU64_ROOT=") + root;
    std::string propsEnv = std::string("AEMU64_PROPS=") + root + "/dev/__properties__";
    std::string libraryEnv = std::string("AEMU64_LIBPATH=") + guestLibraryDir;
    char androidRoot[] = "ANDROID_ROOT=/system";
    char androidData[] = "ANDROID_DATA=/data";
    char* environment[] = {rootEnv.data(), propsEnv.data(), libraryEnv.data(), androidRoot, androidData, nullptr};
    char* arguments[] = {const_cast<char*>(runner), const_cast<char*>(program),
        const_cast<char*>(program), const_cast<char*>(probeMode), nullptr};
    const int spawned = posix_spawn(&child.pid, runner, nullptr, nullptr, arguments, environment);
    if (spawned != 0) return fail("Guest spawn", spawned);

    int exitStatus = 0;
    bool serving = true;
    bool reaped = false;
    bool canSignal = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        // The only admitted PID is our own unreaped child, so it cannot be reused.
        const auto done = waitpid(child.pid, &exitStatus, WNOHANG);
        if (done == child.pid) { reaped = true; break; }
        if (done < 0 && errno != EINTR) { canSignal = false; break; }
        if (serving) serving = server->runOnce(25);
        else usleep(25000);
    }
    if (!reaped && canSignal) {
        kill(child.pid, SIGKILL);
        while (waitpid(child.pid, &exitStatus, 0) < 0 && errno == EINTR) {}
    }
    server.reset();
    android_waitForOpenglesProcessCleanup();
    if (!reaped || !WIFEXITED(exitStatus) || WEXITSTATUS(exitStatus) != 0)
        return fail(reaped ? "Guest exit" : "Guest deadline", exitStatus);
    std::fprintf(stderr, "RME_GUEST_VULKAN_PROBE_EXIT_OK\n");
    return 0;
}

int rme::runGuestApp(const char* root, pid_t pid, int readyFd) {
    AttachedProcess process;
    if (pid > 1 && !process.pin(pid)) {
        close(readyFd);
        return fail("Guest app pidfd", errno);
    }
    Device device;
    if (!device.initialize()) {
        close(readyFd);
        return fail("AndroidPipe initialization", 0);
    }
    SocketPath path{std::string(root) + "/dev/socket/aemuvk"};
    const int listener = listenForGuest(path);
    if (listener < 0) {
        close(readyFd);
        return fail("listen", -listener);
    }
    const auto principal = std::make_shared<const Principal>();
    auto server = SocketServer::create(device, listener, path.name.c_str(),
        AttachedProcess::authorize, &process, principal, ProbeChild::drain);
    if (!server) {
        close(readyFd);
        return fail("AndroidPipe socket server", 0);
    }
    constexpr char ready[] = "RME_VK_GUEST_APP_READY\n";
    const auto reported = write(readyFd, ready, sizeof(ready) - 1);
    close(readyFd);
    if (reported != sizeof(ready) - 1) return fail("Guest app ready", 0);
    if (pid <= 1) {
        pid = readTarget();
        if (pid <= 1 || !process.pin(pid)) return fail("Guest app PID handshake", errno);
    }

    bool attached = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(8);
    while (process.live() && std::chrono::steady_clock::now() < deadline) {
        if (!server->runOnce(25)) break;
        if (!attached && principal->puid()) {
            std::fprintf(stderr, "RME_VK_GUEST_APP_CONNECTED pid=%d\n", pid);
            attached = true;
        }
        pollfd command{STDIN_FILENO, POLLIN, 0};
        if (poll(&command, 1, 0) > 0) {
            char byte;
            if (read(STDIN_FILENO, &byte, 1) == 1 && byte == 'Q') break;
        }
    }
    server.reset();
    android_waitForOpenglesProcessCleanup();
    return attached ? 0 : fail("Guest app did not connect", 0);
}

int rme::runGuest(const char* root, const char* runner, pid_t parent, int readyFd) {
    const int self = syscall(SYS_pidfd_open, getpid(), 0);
    if (self < 0) {
        close(readyFd);
        return fail("Guest process identity", errno);
    }
    close(self);
    struct stat runnerFile{};
    Device device;
    SocketPath path{std::string(root) + "/dev/socket/aemuvk"};
    const int listener = stat(runner, &runnerFile) == 0 && device.initialize()
        ? listenForGuest(path) : -EINVAL;
    if (listener < 0) {
        close(readyFd);
        return fail("Guest listener", -listener);
    }
    GuestIdentity identity{std::string("AEMU64_ROOT=") + root, runnerFile, parent};
    auto server = SocketServer::createForGuest(device, listener, path.name.c_str(),
                                               GuestIdentity::authorize, &identity);
    constexpr char ready[] = "RME_VK_HOST_READY\n";
    const auto reported = server ? write(readyFd, ready, sizeof(ready) - 1) : -1;
    close(readyFd);
    if (reported != sizeof(ready) - 1) return fail("Guest ready", 0);
    int status = 1;
    while (server->runOnce(25)) {
        pollfd command{STDIN_FILENO, POLLIN, 0};
        if (poll(&command, 1, 0) <= 0) continue;
        char byte;
        const auto count = read(STDIN_FILENO, &byte, 1);
        if (count == 0 || (count == 1 && byte == 'Q')) { status = 0; break; }
    }
    server.reset();
    android_waitForOpenglesProcessCleanup();
    return status;
}
