// Narrow RenderControl initialization decoder for a Vulkan-only RenderThread.
// Its PUID must be supplied by a trusted session owner, not by the Guest.
#pragma once
#include "principal_scope.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

class RcDecoder {
public:
    explicit RcDecoder(uint64_t trustedPuid, bool wsiNegotiation = false)
        : trustedPuid_(trustedPuid), wsiNegotiation_(wsiNegotiation) {}
    explicit RcDecoder(std::shared_ptr<const rme::Principal> principal, bool vulkanReady,
                       bool scalarTypes = false)
        : vulkanReady_(vulkanReady), scalarTypes_(scalarTypes), principal_(std::move(principal)) {}

    bool acceptsVulkan(uint64_t puid) const {
        const uint64_t trusted = principal_ ? principal_->puid() : trustedPuid_;
        return trusted && trusted != UINT64_MAX && puid == trusted;
    }

    // Positive: bytes consumed; zero: incomplete or not RenderControl;
    // negative: malformed/unsupported RC, I/O failure, or untrusted PUID.
    template<class Stream>
    int decode(const void* packet, size_t available, Stream* stream, uint64_t* puid) {
        if (!packet || !stream || !puid) return -1;
        if (available < 8) return 0;
        const auto* bytes = static_cast<const uint8_t*>(packet);
        uint32_t op, length;
        memcpy(&op, bytes, 4); memcpy(&length, bytes + 4, 4);
        if (op < 10000 || op >= 10070) return 0;
        uint32_t expected = op == 10000 ? 8 : (op == 10033 || op == 10069 ? 16 : 0);
        if (!expected || length != expected) return -1;
        if (available < expected) return 0;
        if (op == 10033) {
            uint64_t requested;
            memcpy(&requested, bytes + 8, 8);
            if (!acceptsVulkan(requested)) return -1;
            *puid = requested;
            return static_cast<int>(expected);
        }
        if (op == 10000) {
            const int32_t version = 1;  // existing renderControl protocol version
            return stream->writeFully(&version, sizeof(version)) < 0 ? -1 : static_cast<int>(expected);
        }
        uint32_t size, outputSize;
        memcpy(&size, bytes + 8, 4); memcpy(&outputSize, bytes + 12, 4);
        if (size != outputSize || outputSize > 4096) return -1;
        const char* extensions = "";
        if (vulkanReady_ && acceptsVulkan(*puid)) {
            extensions = scalarTypes_ ? "ANDROID_EMU_vulkan ANDROID_EMU_vulkan_shader_float16_int8"
                                      : "ANDROID_EMU_vulkan";
        }
        if (wsiNegotiation_) extensions = "ANDROID_EMU_rme_wsi_protocol_v1";
        const size_t required = std::strlen(extensions) + 1;
        std::vector<uint8_t> output(outputSize, 0);
        if (outputSize >= required) std::memcpy(output.data(), extensions, required);
        const int32_t result = outputSize >= required ? static_cast<int32_t>(required)
                                                       : -static_cast<int32_t>(required);
        if ((!output.empty() && stream->writeFully(output.data(), output.size()) < 0) ||
            stream->writeFully(&result, sizeof(result)) < 0) return -1;
        return static_cast<int>(expected);
    }
private:
    uint64_t trustedPuid_ = 0;
    bool wsiNegotiation_ = false;
    bool vulkanReady_ = false;
    bool scalarTypes_ = false;
    std::shared_ptr<const rme::Principal> principal_;
};
