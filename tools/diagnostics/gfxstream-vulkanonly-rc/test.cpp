#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "gfxstream/guest/IOStream.h"
#include "gfxstream/guest/ChecksumCalculator.h"
#include "renderControl_opcodes.h"
#ifdef RME_REAL_PIPE
#include "channel.h"
#include "host-common/opengl/GLProcessPipe.h"
#endif

using GLint = int32_t;
using gfxstream::guest::IOStream;
struct renderControl_encoder_context_t {
    IOStream* m_stream;
    gfxstream::guest::ChecksumCalculator* m_checksumCalculator;
};
#define ENCODER_DEBUG_LOG(...) ((void)0)
#define AEMU_SCOPED_TRACE(...) ((void)0)
#define ALOGE(...) ((void)0)
#include "actual_encoder.inc"

struct HostStream {
    std::vector<uint8_t> response;
    int writeFully(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        response.insert(response.end(), b, b + n);
        return 0;
    }
};

#ifdef RME_CANDIDATE
#include "rc_decoder.h"
#else
struct RcDecoder {
    explicit RcDecoder(uint64_t) {}
    int decode(const void*, size_t, HostStream*, uint64_t*) { return 0; }
};
#endif

class GuestStream final : public IOStream {
public:
    explicit GuestStream(uint64_t trusted) : IOStream(4096), decoder(trusted) {}
    void* allocBuffer(size_t n) override { buffer.resize(n); return buffer.data(); }
    int commitBuffer(size_t n) override {
        size_t pos = 0;
        while (pos < n) {
            int used = decoder.decode(buffer.data() + pos, n - pos, &host, &puid);
            if (used <= 0) return -1;
            pos += static_cast<size_t>(used);
        }
        return 0;
    }
    const unsigned char* readFully(void* p, size_t n) override {
        if (readPos + n > host.response.size()) return nullptr;
        memcpy(p, host.response.data() + readPos, n);
        readPos += n;
        return static_cast<const unsigned char*>(p);
    }
    const unsigned char* commitBufferAndReadFully(size_t n, void* p, size_t len) override {
        if (commitBuffer(n) < 0) return nullptr;
        return readFully(p, len);
    }
    const unsigned char* read(void* p, size_t* n) override { return readFully(p, *n); }
    int writeFully(const void*, size_t) override { return -1; }
    RcDecoder decoder;
    HostStream host;
    std::vector<uint8_t> buffer;
    size_t readPos = 0;
    uint64_t puid = 0;
};

int main() {
    uint64_t trusted = 42;
#ifdef RME_REAL_PIPE
    rme::Device device;
    if (!device.initialize()) return 20;
    auto pipe = device.open(rme::Service::Process);
    if (!pipe) return 21;
    int32_t confirm = 100;
    if (pipe->send(&confirm, sizeof(confirm)) != sizeof(confirm) ||
        pipe->receive(&trusted, sizeof(trusted)) != sizeof(trusted) || !trusted) return 22;
#endif
    GuestStream peer(trusted);
    gfxstream::guest::ChecksumCalculator checksum;
    renderControl_encoder_context_t ctx{&peer, &checksum};
    char extensions[32]; memset(extensions, 0x7f, sizeof(extensions));
    if (rcGetHostExtensionsString_enc(&ctx, sizeof(extensions), extensions) != 1 || extensions[0]) {
        fprintf(stderr, "FAIL: upstream extension encoder cannot complete Vulkan-only handshake\n");
        return 1;
    }
    rcSetPuid_enc(&ctx, trusted);
    if (rcGetRendererVersion_enc(&ctx) != 1 || peer.puid != trusted) {
        fprintf(stderr, "FAIL: upstream PUID/version encoder exchange\n");
        return 2;
    }
    uint32_t bad[] = {OP_rcCreateContext, 8};
    if (peer.decoder.decode(bad, sizeof(bad), &peer.host, &peer.puid) >= 0) return 3;
    uint32_t wrong[] = {OP_rcSetPuid, 16, 0, 0};
    const uint64_t wrongId = trusted + 1;
    memcpy(wrong + 2, &wrongId, sizeof(wrongId));
    if (peer.decoder.decode(wrong, sizeof(wrong), &peer.host, &peer.puid) >= 0 || peer.puid != trusted) return 4;
    uint32_t malformed[] = {OP_rcGetHostExtensionsString, 16, 16, 15};
    if (peer.decoder.decode(malformed, sizeof(malformed), &peer.host, &peer.puid) >= 0) return 5;
    uint32_t other[] = {20000, 8};
    if (peer.decoder.decode(other, sizeof(other), &peer.host, &peer.puid) != 0) return 6;
    uint32_t fragment[] = {OP_rcSetPuid, 16};
    if (peer.decoder.decode(fragment, sizeof(fragment), &peer.host, &peer.puid) != 0) return 7;
    RcDecoder unbound(0);
    uint32_t valid[] = {OP_rcSetPuid, 16, 0, 0};
    memcpy(valid + 2, &trusted, sizeof(trusted));
    if (unbound.decode(valid, sizeof(valid), &peer.host, &peer.puid) >= 0 || peer.puid != trusted) return 8;
    char empty = 'X';
    if (rcGetHostExtensionsString_enc(&ctx, 0, &empty) != -1 || empty != 'X') return 9;
#ifdef RME_CANDIDATE
    auto principal = std::make_shared<const rme::Principal>();
    if (!principal->bind(trusted)) return 10;
    RcDecoder scalarTypes(principal, true, true);
    uint32_t extensionQuery[] = {OP_rcGetHostExtensionsString, 16, 96, 96};
    uint64_t current = 0;
    HostStream untrusted;
    if (scalarTypes.decode(extensionQuery, sizeof(extensionQuery), &untrusted, &current) != 16 ||
        untrusted.response[0] != 0) return 11;
    HostStream negotiated;
    if (scalarTypes.decode(valid, sizeof(valid), &negotiated, &current) != 16 ||
        scalarTypes.decode(extensionQuery, sizeof(extensionQuery), &negotiated, &current) != 16 ||
        std::strcmp(reinterpret_cast<const char*>(negotiated.response.data()),
                    "ANDROID_EMU_vulkan ANDROID_EMU_vulkan_shader_float16_int8")) return 12;
#endif
#ifdef RME_REAL_PIPE
    pipe.reset();
    unsigned live = 0;
    android::opengl::forEachProcessPipeId([&](uint64_t id) { if (id == trusted) ++live; });
    if (live) return 23;
    puts("REAL_PROCESS_PIPE_CLOSED_OK");
#endif
    puts("PASS: upstream RC encoders exchange extensions, PUID and version with Vulkan-only decoder");
}
