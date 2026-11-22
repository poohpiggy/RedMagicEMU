/*
 * Guest Codec2 adapter for the NX809J media APEX. Its format interface comes
 * from the original component factory. The VM enables it with the host relay.
 */
#include <__config_site>
#undef _LIBCPP_ABI_NAMESPACE
#define _LIBCPP_ABI_NAMESPACE __1

#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>
#include <SimpleC2Component.h>
#include <algorithm>
#include <android/log.h>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <optional>
#include <vector>
#include <video_client.h>
#include "../grallocwrap/video_usage.h"

namespace {

constexpr char socketPath[] = "/dev/socket/rme-video-test";
#if RME_HEVC
constexpr char originalPath[] = "/apex/com.android.media.swcodec/lib64/libcodec2_soft_hevcdec.rme-original.so";
constexpr uint32_t codec = VIDEO_HEVC;
#else
constexpr char originalPath[] = "/apex/com.android.media.swcodec/lib64/libcodec2_soft_avcdec.rme-original.so";
constexpr uint32_t codec = VIDEO_AVC;
#endif

class HostDecoder final : public android::SimpleC2Component {
public:
    explicit HostDecoder(const std::shared_ptr<C2ComponentInterface> &intf) : SimpleC2Component(intf) {}
    ~HostDecoder() override { closeHost(); }

protected:
    c2_status_t onInit() override { return C2_OK; }
    c2_status_t onStop() override { closeHost(); return C2_OK; }
    void onReset() override { closeHost(); }
    void onRelease() override { closeHost(); }
    c2_status_t onFlush_sm() override {
        pending.clear();
        if (host >= 0 && rme_video_flush(host) != VIDEO_OK) {
            closeHost();
            return C2_CORRUPTED;
        }
        return C2_OK;
    }

    c2_status_t drain(uint32_t mode, const std::shared_ptr<C2BlockPool> &) override {
        return mode == NO_DRAIN ? C2_OK : C2_OMITTED;
    }

    void process(const std::unique_ptr<C2Work> &work, const std::shared_ptr<C2BlockPool> &pool) override {
        work->result = C2_OK;
        work->workletsProcessed = 0;
        const bool config = (work->input.flags & C2FrameData::FLAG_CODEC_CONFIG) != 0;
        const bool eos = (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0;
        const int64_t pts = work->input.ordinal.timestamp.peekll();
        const uint64_t index = work->input.ordinal.frameIndex.peekull();
        size_t size = 0;
        const uint8_t *input = nullptr;
        std::optional<C2ReadView> view;
        if (!work->input.buffers.empty()) {
            auto blocks = work->input.buffers.front()->data().linearBlocks();
            if (blocks.size() != 1) { fail(work, C2_BAD_VALUE); return; }
            view.emplace(blocks.front().map().get());
            if (view->error() != C2_OK) { fail(work, view->error()); return; }
            size = view->capacity();
            input = view->data();
        }
        if (!size && !eos) { fill(work, nullptr, false); return; }
        if (size > RME_VIDEO_MAX_SAMPLE || pts < 0 || !openHost()) {
            fail(work, C2_BAD_VALUE);
            return;
        }
        uint32_t flags = (config ? VIDEO_CONFIG : 0) | (eos ? VIDEO_EOS : 0);
        int attempts = eos ? 500 : 200;
        uint32_t submit = VIDEO_AGAIN;
        while (submit == VIDEO_AGAIN && attempts-- > 0) {
            submit = rme_video_submit(host, input, static_cast<uint32_t>(size), pts, flags);
            if (submit == VIDEO_AGAIN && receive(work, pool) == VIDEO_ERROR) {
                fail(work, C2_CORRUPTED);
                return;
            }
        }
        if (submit != VIDEO_OK) { fail(work, C2_CORRUPTED); return; }
        if (!config && size) pending.emplace(pts, index);
        bool reachedEos = false;
        do {
            uint32_t status = receive(work, pool, &reachedEos);
            if (status == VIDEO_ERROR) { fail(work, C2_CORRUPTED); return; }
            if (!eos && status == VIDEO_AGAIN) break;
        } while (!reachedEos && attempts-- > 0);
        if (eos && !reachedEos) { fail(work, C2_TIMED_OUT); return; }
        if ((config || eos) && !work->workletsProcessed) fill(work, nullptr, eos);
    }

private:
    int host = -1;
    video_open config = {};
    uint32_t outputWidth = 0, outputHeight = 0;
    std::vector<uint8_t> frame;
    std::multimap<int64_t, uint64_t> pending;

    bool openHost() {
        if (host >= 0) return true;
        C2StreamPictureSizeInfo::output size(0u, 0u, 0u);
        std::vector<std::unique_ptr<C2Param>> heap;
        if (intf()->query_vb({&size}, {}, C2_MAY_BLOCK, &heap) != C2_OK ||
            !video_dimensions(size.width, size.height)) return false;
        config = {.magic=RME_VIDEO_MAGIC, .codec=codec, .width=size.width, .height=size.height};
        C2StreamMaxPictureSizeTuning::output maximum(0u, 0u, 0u);
        if (intf()->query_vb({&maximum}, {}, C2_MAY_BLOCK, &heap) == C2_OK) {
            const uint32_t maxWidth = std::min(maximum.width, RME_VIDEO_MAX_WIDTH) & ~1u;
            const uint32_t maxHeight = std::min(maximum.height, RME_VIDEO_MAX_HEIGHT) & ~1u;
            if (maxWidth >= size.width && maxHeight >= size.height) {
                config.max_width = maxWidth;
                config.max_height = maxHeight;
            }
        }
        video_format format = {};
        host = rme_video_open(socketPath, &config, nullptr, nullptr, &format);
        if (host < 0) return false;
        outputWidth = size.width;
        outputHeight = size.height;
        frame.resize(video_frame_size(config.max_width ? config.max_width : size.width,
                                      config.max_height ? config.max_height : size.height));
        __android_log_print(ANDROID_LOG_INFO, "RmeCodec2", "connected to %s %ux%u (adaptive max %ux%u)",
                            format.decoder, size.width, size.height, config.max_width, config.max_height);
        return true;
    }

    void closeHost() {
        if (host >= 0) close(host);
        host = -1;
        pending.clear();
        frame.clear();
    }

    static void fill(const std::unique_ptr<C2Work> &work, const std::shared_ptr<C2Buffer> &buffer, bool eos,
                     const C2StreamPictureSizeInfo::output *size = nullptr) {
        auto &output = work->worklets.front()->output;
        output.flags = eos ? C2FrameData::FLAG_END_OF_STREAM : (C2FrameData::flags_t)0;
        output.ordinal = work->input.ordinal;
        output.buffers.clear();
        if (buffer) output.buffers.push_back(buffer);
        if (size) output.configUpdate.push_back(C2Param::Copy(*size));
        work->workletsProcessed = 1;
    }

    static void fail(const std::unique_ptr<C2Work> &work, c2_status_t status) {
        __android_log_print(ANDROID_LOG_ERROR, "RmeCodec2", "decoder failed (%d)", status);
        fill(work, nullptr, false);
        work->result = status;
    }

    uint32_t receive(const std::unique_ptr<C2Work> &current, const std::shared_ptr<C2BlockPool> &pool,
                     bool *eos = nullptr) {
        video_packet output;
        const uint32_t state = rme_video_receive(host, frame.data(), frame.size(), &output);
        if (state != VIDEO_OK) return state;
        if (eos) *eos = (output.flags & VIDEO_EOS) != 0;
        if (!output.size) return VIDEO_OK;
        uint32_t width, height;
        if (!video_frame_dimensions(&output, &width, &height) ||
            !video_output_dimensions(&config, width, height)) return VIDEO_ERROR;
        auto packet = pending.find(output.pts_us);
        if (packet == pending.end()) return VIDEO_ERROR;
        auto index = packet->second;
        pending.erase(packet);
        std::shared_ptr<C2GraphicBlock> block;
        C2MemoryUsage usage = {C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE};
        uint32_t format = HAL_PIXEL_FORMAT_YV12;
#if RME_HEVC
        // Use the same byte-buffer layout as the AVC encoder for flexible YUV
        // transcodes. The Surface buffer queue continues to use planar YV12.
        if (pool->getAllocatorId() == android::C2PlatformAllocatorStore::GRALLOC) {
            format = HAL_PIXEL_FORMAT_YCBCR_420_888;
            usage.expected |= RME_GRALLOC_USAGE_NV12;
        }
#endif
        if (pool->fetchGraphicBlock(width, height, format, usage, &block) != C2_OK) {
            return VIDEO_ERROR;
        }
        {
            C2GraphicView view = block->map().get();
            if (view.error() != C2_OK || view.layout().type != C2PlanarLayout::TYPE_YUV ||
                view.layout().numPlanes != 3) return VIDEO_ERROR;
            const uint8_t *input = frame.data();
            for (unsigned plane = 0; plane < 3; plane++) {
                const auto &layout = view.layout().planes[plane];
                const uint32_t w = plane ? width/2 : width;
                const uint32_t h = plane ? height/2 : height;
                if (layout.rowInc <= 0 || layout.colInc <= 0 || layout.allocatedDepth != 8) return VIDEO_ERROR;
                uint8_t *dest = view.data()[plane];
                for (uint32_t row = 0; row < h; row++) {
                    if (layout.colInc == 1) memcpy(dest, input, w);
                    else for (uint32_t col=0; col<w; col++) dest[col*layout.colInc] = input[col];
                    dest += layout.rowInc;
                    input += w;
                }
            }
        }
        auto buffer = createGraphicBuffer(block, C2Rect(width, height));
        C2StreamPictureSizeInfo::output size(0u, width, height);
        const bool changed = width != outputWidth || height != outputHeight;
        if (changed) {
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            if (intf()->config_vb({&size}, C2_MAY_BLOCK, &failures) != C2_OK ||
                size.width != width || size.height != height) return VIDEO_ERROR;
            outputWidth = width;
            outputHeight = height;
        }
        const bool isCurrent = index == current->input.ordinal.frameIndex.peekull();
        if (isCurrent) fill(current, buffer, (output.flags & VIDEO_EOS) != 0, changed ? &size : nullptr);
        else finish(index, [buffer, size, changed](const std::unique_ptr<C2Work> &done) {
            fill(done, buffer, false, changed ? &size : nullptr);
        });
        return VIDEO_OK;
    }
};

class HostFactory final : public C2ComponentFactory {
public:
    HostFactory(void *handle, C2ComponentFactory *factory, DestroyCodec2FactoryFunc destroy)
        : library(handle), original(factory), destroyOriginal(destroy) {}
    ~HostFactory() override { destroyOriginal(original); dlclose(library); }

    c2_status_t createComponent(c2_node_id_t id, std::shared_ptr<C2Component> *component,
                                ComponentDeleter deleter) override {
        std::shared_ptr<C2ComponentInterface> intf;
        c2_status_t result = original->createInterface(id, &intf);
        if (result != C2_OK) return result;
        *component = std::shared_ptr<C2Component>(new HostDecoder(intf), std::move(deleter));
        return C2_OK;
    }

    c2_status_t createInterface(c2_node_id_t id, std::shared_ptr<C2ComponentInterface> *intf,
                                InterfaceDeleter deleter) override {
        return original->createInterface(id, intf, std::move(deleter));
    }
private:
    void *library;
    C2ComponentFactory *original;
    DestroyCodec2FactoryFunc destroyOriginal;
};

}  // namespace

extern "C" __attribute__((visibility("default"))) C2ComponentFactory *CreateCodec2Factory() {
    // Codec2 can retain interfaces after it discards a factory during discovery.
    // Their virtual methods and deleters still refer to the original DSO.
    void *library = dlopen(originalPath, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library) return nullptr;
    auto create = reinterpret_cast<C2ComponentFactory::CreateCodec2FactoryFunc>(dlsym(library, "CreateCodec2Factory"));
    auto destroy = reinterpret_cast<C2ComponentFactory::DestroyCodec2FactoryFunc>(dlsym(library, "DestroyCodec2Factory"));
    C2ComponentFactory *original = (create && destroy) ? create() : nullptr;
    if (!original) { dlclose(library); return nullptr; }
    return new HostFactory(library, original, destroy);
}

extern "C" __attribute__((visibility("default"))) void DestroyCodec2Factory(C2ComponentFactory *factory) {
    delete factory;
}
