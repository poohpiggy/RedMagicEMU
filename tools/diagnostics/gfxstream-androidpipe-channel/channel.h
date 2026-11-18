#pragma once
#include <cstddef>
#include <memory>

namespace rme {
enum class Service { OpenGLES, Process };
class Session;

// Research-only local channel. No listener or authentication is provided.
// All calls and Session destruction must occur on the initializing thread.
class Device {
public:
    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    bool initialize();
    std::unique_ptr<Session> open(Service service);
    // Borrowed descriptor for the device thread's event loop; never close it.
    int wakeupFd() const;
    // Run queued upstream callbacks on the initializing thread under VM lock.
    bool dispatchWakeups();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Session {
public:
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    int send(const void* data, size_t size);
    int receive(void* data, size_t size);
    unsigned poll();
    bool watch(unsigned flags);
private:
    friend class Device;
    struct Impl;
    explicit Session(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}
