#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace std {
struct mutex {};
template <typename T> struct lock_guard { explicit lock_guard(T&) {} };
}
void* operator new(unsigned long size) { return malloc(size); }
void operator delete(void* p) noexcept { free(p); }

enum HostConnectionType { HOST_CONNECTION_QEMU_PIPE, HOST_CONNECTION_ADDRESS_SPACE,
                          HOST_CONNECTION_VIRTIO_GPU_PIPE, HOST_CONNECTION_VIRTIO_GPU_ADDRESS_SPACE };
struct VirtioGpuPipeStream { VirtioGpuPipeStream(int, int) {} uint64_t initProcessPipe() { return 12; } };
static VirtioGpuPipeStream* sVirtioGpuPipeStream;
using QEMU_PIPE_HANDLE = int;
constexpr int QEMU_PIPE_INVALID_HANDLE = -1;
struct VirtGpuDevice { static void resetInstance() {} };
static int sStreamHandle = -1, mode, attempts, closed, rc_calls;
static uint64_t sProcUID;
static HostConnectionType sConnType;
static uint32_t* sSeqnoPtr;
static std::mutex sNeedInitMutex;
static bool sNeedInit = true;
static void initSeqno() { delete sSeqnoPtr; sSeqnoPtr = new uint32_t; *sSeqnoPtr = 0; }
static int qemu_pipe_open(const char*) { ++attempts; return mode == 1 ? -1 : mode == 4 ? 0 : 7; }
static bool qemu_pipe_valid(int fd) { return fd >= 0; }
static int qemu_pipe_write_fully(int, const void*, int) { return mode == 2 ? -1 : 0; }
static int qemu_pipe_read_fully(int, void* p, int) { if (mode == 3) return -1; *static_cast<uint64_t*>(p) = 42; return 0; }
static void qemu_pipe_close(int) { ++closed; }
#define ALOGW(...) ((void)0)
#define ALOGE(...) ((void)0)
#include "process.inc"

enum VirtGpuCapset { kCapsetGfxStreamVulkan, kCapsetNone };
struct HostConnection { int m_rendernodeFd = -1; void rcEncoder() { ++rc_calls; } };
static HostConnection connection;
static HostConnection* connectTest(HostConnectionType connType, uint32_t noRenderControlEnc, VirtGpuCapset capset) {
    HostConnection* con = &connection;
#include "connect.inc"

int main(int argc, char** argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "open")) mode = 1;
    else if (!strcmp(argv[1], "write")) mode = 2;
    else if (!strcmp(argv[1], "read")) mode = 3;
    else if (!strcmp(argv[1], "retry")) mode = 1;
    else if (!strcmp(argv[1], "host")) mode = 1;
    else if (!strcmp(argv[1], "fd_zero")) mode = 4;
    if (mode == 4) {
        assert(processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 0));
        assert(attempts == 1 && sProcUID == 42);
        processPipeRestart();
        assert(closed == 1);
        return 0;
    }
    if (!strcmp(argv[1], "restart_no_pipe")) {
        assert(processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 1));
        processPipeRestart();
        assert(closed == 0);
        return 0;
    }
    if (mode && strcmp(argv[1], "host")) {
        assert(!processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 0));
        if (!strcmp(argv[1], "retry")) {
            mode = 0;
            assert(processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 0));
            assert(attempts == 2 && sProcUID == 42);
        } else {
            assert(!processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 0));
            assert(attempts == 2 && closed == (mode == 1 ? 0 : 2));
        }
    } else if (!strcmp(argv[1], "host")) {
        assert(connectTest(HOST_CONNECTION_QEMU_PIPE, 0, kCapsetGfxStreamVulkan) == nullptr);
        assert(rc_calls == 0);
        mode = 0;
        assert(connectTest(HOST_CONNECTION_QEMU_PIPE, 0, kCapsetGfxStreamVulkan) == &connection);
        assert(attempts == 2 && rc_calls == 1 && sProcUID == 42);
    } else if (!strcmp(argv[1], "no_render_control")) {
        assert(processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 1));
        assert(processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 1) && attempts == 0);
    } else {
        assert(processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 0));
        assert(processPipeInit(-1, HOST_CONNECTION_QEMU_PIPE, 0) && attempts == 1 && sProcUID == 42);
    }
}
