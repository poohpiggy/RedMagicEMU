#include "abi.h"
#include "../guest_process_identity.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <dlfcn.h>
#include <jni.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <thread>

namespace {
using Milliseconds = std::chrono::milliseconds;

class Fd {
public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
    int release() { const int fd = fd_; fd_ = -1; return fd; }
private:
    int fd_;
};

std::string systemError(const char* operation) {
    return std::string(operation) + " (errno " + std::to_string(errno) + ')';
}

int pollUninterrupted(pollfd* fds, nfds_t size, int timeout) {
    int result;
    do { result = poll(fds, size, timeout); } while (result < 0 && errno == EINTR);
    return result;
}

bool live(int pidfd) {
    pollfd death{pidfd, POLLIN, 0};
    return pidfd >= 0 && pollUninterrupted(&death, 1, 0) == 0;
}

class Service {
public:
    Service(const std::string& root, const std::string& runner) :
        path_(root + RME_CL_SOCKET_PATH), runner_(runner), rootEnvironment_("AEMU64_ROOT=" + root),
        wake_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
        if (wake_.get() < 0) throw std::runtime_error(systemError("OpenCL wake fd"));
    }

    std::string awaitReady(Milliseconds timeout) {
        std::unique_lock lock(mutex_);
        if (!changed_.wait_for(lock, timeout, [&] { return startupReported_ || cancelled_; }))
            return "host startup timed out";
        return cancelled_ ? "host startup cancelled" : startupError_;
    }

    void cancel() {
        std::lock_guard lock(mutex_);
        cancelLocked();
    }

    std::string awaitStopped(Milliseconds timeout) {
        std::unique_lock lock(mutex_);
        cancelLocked();
        if (!changed_.wait_for(lock, timeout, [&] { return stopped_; }))
            return "host worker still draining";
        if (!removeSocket()) return systemError("remove OpenCL socket");
        return cleanupError_;
    }

    void run() {
        void* driver = nullptr;
        int listener = -1;
        try {
            struct stat runnerFile{};
            Fd self(syscall(SYS_pidfd_open, getpid(), 0));
            if (self.get() < 0) throw std::runtime_error(systemError("OpenCL peer pidfd"));
            if (stat(runner_.c_str(), &runnerFile)) throw std::runtime_error(systemError("OpenCL Guest runner"));
            const rme::GuestIdentity identity{rootEnvironment_, runnerFile, getpid()};
            // OpenCL releases may leave queued vendor work in flight. Release our loader
            // reference after draining while retaining the mapped code until process exit.
            driver = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
            if (!driver) throw std::runtime_error(dlerror());
            const auto status = rme_cl_abi_check_driver(driver);
            if (status) throw std::runtime_error("host driver check failed (OpenCL " + std::to_string(status) + ')');
            {
                std::lock_guard lock(mutex_);
                if (!cancelled_) listener = openSocket();
            }
            reportStartup({});
            if (listener >= 0) serve(listener, identity, driver);
        } catch (const std::exception& error) {
            reportStartup(error.what());
        }
        if (listener >= 0) close(listener);
        removeSocket();
        {
            std::unique_lock lock(mutex_);
            if (startupReported_ && startupError_.empty() && !cancelled_)
                startupError_ = "host listener exited";
            // Keep the startup error observable while stopping after startup failure.
            shutdownClientsLocked();
            changed_.wait(lock, [&] { return reapClientsLocked(); });
        }
        if (driver && dlclose(driver)) cleanupError_ = dlerror();
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            changed_.notify_all();
        }
    }

private:
    static constexpr size_t MaxClients = 16;

    struct Client {
        int fd = -1;
        int pidfd = -1;
        bool finished = false;
        std::thread worker;
    };

    int openSocket() {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (path_.size() >= sizeof(address.sun_path)) throw std::runtime_error("OpenCL socket path too long");
        std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);
        struct stat stale{};
        if (!lstat(path_.c_str(), &stale)) {
            if (!S_ISSOCK(stale.st_mode) || stale.st_uid != getuid())
                throw std::runtime_error("unexpected file at OpenCL socket path");
            if (unlink(path_.c_str())) throw std::runtime_error(systemError("remove stale OpenCL socket"));
        } else if (errno != ENOENT) {
            throw std::runtime_error(systemError("inspect OpenCL socket path"));
        }
        Fd listener(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
        if (listener.get() < 0) throw std::runtime_error(systemError("create OpenCL socket"));
        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)))
            throw std::runtime_error(systemError("bind OpenCL socket"));
        bound_ = true;
        if (chmod(path_.c_str(), S_IRUSR | S_IWUSR) || listen(listener.get(), MaxClients))
            throw std::runtime_error(systemError("listen on OpenCL socket"));
        return listener.release();
    }

    bool removeSocket() {
        if (bound_ && unlink(path_.c_str()) && errno != ENOENT) return false;
        bound_ = false;
        return true;
    }

    void reportStartup(std::string error) {
        std::lock_guard lock(mutex_);
        startupError_ = std::move(error);
        startupReported_ = true;
        changed_.notify_all();
    }

    void wakeListener() {
        int result;
        do { result = eventfd_write(wake_.get(), 1); } while (result < 0 && errno == EINTR);
    }

    void shutdownClientsLocked() {
        for (auto& client : clients_) if (client.fd >= 0) shutdown(client.fd, SHUT_RDWR);
    }

    void cancelLocked() {
        if (cancelled_) return;
        cancelled_ = true;
        shutdownClientsLocked();
        wakeListener();
        changed_.notify_all();
    }

    bool reapClientsLocked() {
        bool drained = true;
        for (auto& client : clients_) {
            if (client.finished) {
                client.worker.join();
                close(client.fd);
                if (client.pidfd >= 0) close(client.pidfd);
                client.fd = client.pidfd = -1;
                client.finished = false;
            }
            if (client.fd >= 0) drained = false;
        }
        return drained;
    }

    void serve(int listener, const rme::GuestIdentity& identity, void* driver) {
        std::array<pollfd, MaxClients + 2> fds{{{listener, POLLIN, 0}, {wake_.get(), POLLIN, 0}}};
        for (;;) {
            {
                std::lock_guard lock(mutex_);
                if (cancelled_) return;
                reapClientsLocked();
                for (size_t i = 0; i < MaxClients; ++i) fds[i + 2] = {clients_[i].pidfd, POLLIN, 0};
            }
            if (pollUninterrupted(fds.data(), fds.size(), -1) < 0)
                throw std::runtime_error(systemError("poll OpenCL socket"));
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
                throw std::runtime_error("OpenCL listener stopped unexpectedly");
            if (fds[1].revents & POLLIN) {
                eventfd_t unused;
                eventfd_read(wake_.get(), &unused);
            }
            {
                std::lock_guard lock(mutex_);
                if (cancelled_) return;
                for (size_t i = 0; i < MaxClients; ++i) {
                    if (!fds[i + 2].revents) continue;
                    auto& client = clients_[i];
                    shutdown(client.fd, SHUT_RDWR);
                    close(client.pidfd);
                    client.pidfd = -1;
                }
            }
            if (fds[0].revents & POLLIN) acceptGuest(listener, identity, driver);
        }
    }

    void acceptGuest(int listener, const rme::GuestIdentity& identity, void* driver) {
        Fd fd(accept4(listener, nullptr, nullptr, SOCK_CLOEXEC));
        if (fd.get() < 0) return;
        ucred peer{};
        socklen_t size = sizeof(peer);
        if (getsockopt(fd.get(), SOL_SOCKET, SO_PEERCRED, &peer, &size) || size != sizeof(peer) ||
            peer.uid != getuid() || peer.pid <= 1) return;
        Fd pidfd(syscall(SYS_pidfd_open, peer.pid, 0));
        if (!live(pidfd.get()) || !identity.owns(peer.pid) || !live(pidfd.get())) return;
        std::lock_guard lock(mutex_);
        if (cancelled_) return;
        for (auto& client : clients_) {
            if (client.fd >= 0) continue;
            const int accepted = fd.get();
            client.worker = std::thread([this, &client, accepted, driver] {
                rme_cl_abi_serve_fd(accepted, driver);
                std::lock_guard lock(mutex_);
                client.finished = true;
                wakeListener();
                changed_.notify_all();
            });
            client.fd = fd.release();
            client.pidfd = pidfd.release();
            return;
        }
    }

    const std::string path_, runner_, rootEnvironment_;
    const Fd wake_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::array<Client, MaxClients> clients_{};
    bool bound_ = false, startupReported_ = false, cancelled_ = false, stopped_ = false;
    std::string startupError_, cleanupError_;
};

using Handle = std::shared_ptr<Service>;

std::string readPath(JNIEnv* env, jstring path) {
    const char* value = env->GetStringUTFChars(path, nullptr);
    if (!value) throw std::runtime_error("cannot read OpenCL socket path");
    const std::string result(value);
    env->ReleaseStringUTFChars(path, value);
    if (result.empty() || result.front() != '/') throw std::runtime_error("OpenCL path must be absolute");
    return result;
}

jstring resultString(JNIEnv* env, const std::string& result) {
    return result.empty() ? nullptr : env->NewStringUTF(result.c_str());
}
}  // namespace

extern "C" JNIEXPORT jlong JNICALL Java_app_aemu_core_HostOpenClVmHost_nativeStart(
        JNIEnv* env, jobject, jstring root, jstring runner) {
    try {
        auto service = std::make_shared<Service>(readPath(env, root), readPath(env, runner));
        auto owner = std::make_unique<Handle>(service);
        std::thread([service] { service->run(); }).detach();
        return reinterpret_cast<jlong>(owner.release());
    } catch (const std::exception& error) {
        if (!env->ExceptionCheck()) env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), error.what());
        return 0;
    }
}

extern "C" JNIEXPORT jstring JNICALL Java_app_aemu_core_HostOpenClVmHost_nativeAwaitReady(
        JNIEnv* env, jobject, jlong handle, jint timeout) {
    return resultString(env, (*reinterpret_cast<Handle*>(handle))->awaitReady(Milliseconds(timeout)));
}

extern "C" JNIEXPORT void JNICALL Java_app_aemu_core_HostOpenClVmHost_nativeCancel(
        JNIEnv*, jobject, jlong handle) {
    (*reinterpret_cast<Handle*>(handle))->cancel();
}

extern "C" JNIEXPORT jstring JNICALL Java_app_aemu_core_HostOpenClVmHost_nativeAwaitStopped(
        JNIEnv* env, jobject, jlong handle, jint timeout) {
    return resultString(env, (*reinterpret_cast<Handle*>(handle))->awaitStopped(Milliseconds(timeout)));
}

extern "C" JNIEXPORT void JNICALL Java_app_aemu_core_HostOpenClVmHost_nativeRelease(
        JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<Handle*>(handle);
}
