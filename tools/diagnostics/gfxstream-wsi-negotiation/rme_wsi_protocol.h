#pragma once
#include <cstdint>
#include <string>

// Outside both upstream Vulkan opcode ranges. This is only sent after the
// existing RenderControl extension query reports the matching protocol.
constexpr uint32_t RME_WSI_NEGOTIATE_OPCODE = 0x524d4555u;
constexpr uint32_t RME_WSI_PROTOCOL_VERSION = 1;
constexpr char RME_WSI_EXTENSION[] = "ANDROID_EMU_rme_wsi_protocol_v1";

inline uint32_t rme_wsi_select_version(uint32_t packetLength, uint32_t requested) {
    return packetLength == 3 * sizeof(uint32_t) && requested == RME_WSI_PROTOCOL_VERSION
        ? RME_WSI_PROTOCOL_VERSION : 0;
}

inline bool rme_wsi_has_extension(const std::string& extensions) {
    size_t start = 0;
    while (start < extensions.size()) {
        const size_t end = extensions.find(' ', start);
        const size_t length = (end == std::string::npos ? extensions.size() : end) - start;
        if (extensions.compare(start, length, RME_WSI_EXTENSION) == 0) return true;
        if (end == std::string::npos) return false;
        start = end + 1;
    }
    return false;
}

template<class Stream>
bool rme_wsi_negotiate(Stream* stream, const std::string& extensions) {
    // Older services do not have this token: do not send or wait for a reply.
    if (!stream || !rme_wsi_has_extension(extensions)) return false;
    const uint32_t request[] = {RME_WSI_NEGOTIATE_OPCODE, 3 * sizeof(uint32_t), RME_WSI_PROTOCOL_VERSION};
    uint32_t reply = 0;
    return stream->flush() >= 0 && stream->writeFully(request, sizeof(request)) == 0 &&
           stream->readFully(&reply, sizeof(reply)) && reply == RME_WSI_PROTOCOL_VERSION;
}
