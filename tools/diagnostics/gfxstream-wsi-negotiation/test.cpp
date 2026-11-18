#include "rme_wsi_protocol.h"
#include "../gfxstream-vulkanonly-rc/rc_decoder.h"
#include <cassert>
#include <cstring>
#include <vector>

struct RcOutput {
    std::vector<unsigned char> data;
    int writeFully(const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        data.insert(data.end(), b, b + n);
        return 0;
    }
};

struct Stream {
    int flushes = 0, writes = 0, reads = 0;
    bool failFlush = false, failWrite = false, failRead = false;
    uint32_t accepted = 0;
    int flush() { ++flushes; return failFlush ? -1 : 0; }
    int writeFully(const void* data, size_t length) {
        ++writes;
        if (failWrite || length != 3 * sizeof(uint32_t)) return -1;
        uint32_t request[3];
        std::memcpy(request, data, sizeof(request));
        assert(request[0] == RME_WSI_NEGOTIATE_OPCODE && request[1] == length);
        accepted = rme_wsi_select_version(request[1], request[2]);
        return 0;
    }
    const unsigned char* readFully(void* data, size_t length) {
        ++reads;
        if (failRead || length != sizeof(accepted)) return nullptr;
        std::memcpy(data, &accepted, length);
        return static_cast<const unsigned char*>(data);
    }
};

int main() {
    const uint32_t getExtensions[] = {10069, 16, 64, 64};
    uint64_t puid = 0;
    RcOutput disabledOutput;
    RcDecoder disabledRc(42, false);
    assert(disabledRc.decode(getExtensions, sizeof(getExtensions), &disabledOutput, &puid) == 16);
    assert(disabledOutput.data[0] == 0);
    RcOutput enabledOutput;
    RcDecoder enabledRc(42, true);
    assert(enabledRc.decode(getExtensions, sizeof(getExtensions), &enabledOutput, &puid) == 16);
    assert(std::strcmp(reinterpret_cast<const char*>(enabledOutput.data.data()), RME_WSI_EXTENSION) == 0);
    Stream legacy;
    assert(!rme_wsi_negotiate(&legacy, "ANDROID_EMU_vulkan"));
    assert(!legacy.flushes && !legacy.writes && !legacy.reads);
    assert(!rme_wsi_negotiate(&legacy, "ANDROID_EMU_rme_wsi_protocol_v10"));
    assert(!legacy.writes && !legacy.reads);
    Stream supported;
    assert(rme_wsi_negotiate(&supported, "first ANDROID_EMU_rme_wsi_protocol_v1 last"));
    assert(supported.accepted == 1 && supported.flushes == 1 && supported.writes == 1 && supported.reads == 1);
    Stream differentConnection;
    assert(differentConnection.accepted == 0);
    assert(rme_wsi_select_version(12, 2) == 0);
    assert(rme_wsi_select_version(8, 1) == 0);
    Stream failed; failed.failWrite = true;
    assert(!rme_wsi_negotiate(&failed, RME_WSI_EXTENSION) && failed.reads == 0);
    Stream noReply; noReply.failRead = true;
    assert(!rme_wsi_negotiate(&noReply, RME_WSI_EXTENSION));
}
