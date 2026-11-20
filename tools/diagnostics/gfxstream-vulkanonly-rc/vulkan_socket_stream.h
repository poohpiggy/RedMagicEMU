#pragma once

#include "../gfxstream-androidpipe-channel/vulkan_socket.h"
#include "render-utils/IOStream.h"
#include <exception>

namespace rme {
class VulkanSocketStream final : public gfxstream::IOStream {
public:
    VulkanSocketStream(VulkanSocket& socket, gfxstream::IOStream& channel, size_t bufferSize)
        : IOStream(bufferSize), socket_(socket), channel_(channel) {}
    ~VulkanSocketStream() { socket_.stop(); }

    void* allocBuffer(size_t size) override {
        output_.resize(size);
        return output_.data();
    }
    int commitBuffer(size_t size) override {
        return socket_.write(output_.data(), size) ? static_cast<int>(size) : -1;
    }
    int writeFully(const void* input, size_t size) override {
        return flush() >= 0 && socket_.write(input, size) ? 0 : -1;
    }
    const unsigned char* readFully(void* output, size_t size) override {
        auto* bytes = static_cast<unsigned char*>(output);
        while (size) {
            const auto count = socket_.read(bytes, size);
            if (count <= 0) return nullptr;
            bytes += count;
            size -= count;
        }
        return static_cast<const unsigned char*>(output);
    }
    void* getDmaForReading(uint64_t address) override {
        return channel_.getDmaForReading(address);
    }
    void unlockDma(uint64_t address) override { channel_.unlockDma(address); }

private:
    const unsigned char* readRaw(void* output, size_t* size) override {
        const auto count = socket_.read(output, *size);
        if (count <= 0) return nullptr;
        *size = static_cast<size_t>(count);
        return static_cast<const unsigned char*>(output);
    }
    // The authenticated headless renderer has no snapshot support.
    void onSave(android::base::Stream*) override { std::terminate(); }
    unsigned char* onLoad(android::base::Stream*) override { std::terminate(); }

    VulkanSocket& socket_;
    gfxstream::IOStream& channel_;
    std::vector<unsigned char> output_;
};
}
