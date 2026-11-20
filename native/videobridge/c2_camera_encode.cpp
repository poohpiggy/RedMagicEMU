/* Guest AVC component backed by the Android host encoder for high frame rates. */
#include <__config_site>
#undef _LIBCPP_ABI_NAMESPACE
#define _LIBCPP_ABI_NAMESPACE __1

#include <C2ComponentFactory.h>
#include <C2PlatformSupport.h>
#include <SimpleC2Component.h>
#include <android/log.h>
#include <arm_neon.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <sys/time.h>
#include <sys/un.h>
#include <vector>
#include "camera_encode_wire.h"

namespace {

constexpr char originalPath[] = "/apex/com.android.media.swcodec/lib64/libcodec2_soft_avcenc.rme-original.so";
constexpr char socketPath[] = "/dev/socket/" RME_CAMERA_ENCODE_SOCKET;
constexpr char logTag[] = "RmeCameraEncoder";

bool profile(const std::shared_ptr<C2ComponentInterface> &intf, camera_encode_open *config) {
    C2StreamPictureSizeInfo::input size(0u, 0u, 0u);
    C2StreamFrameRateInfo::output rate(0u, 0.0f);
    C2StreamBitrateInfo::output bitrate(0u, 0u);
    std::vector<std::unique_ptr<C2Param>> response;
    const c2_status_t query = intf->query_vb({&size, &rate, &bitrate}, {}, C2_MAY_BLOCK, &response);
    // Nubia passes the lower bound of its 50–60 fps sensor range to MediaRecorder.
    // The recording still carries the actual timestamps of the delivered frames.
    if (query != C2_OK || !size || !rate || !bitrate ||
        (rate.value != 50.0f && rate.value != 60.0f && rate.value != 120.0f)) return false;
    *config = {RME_CAMERA_ENCODE_MAGIC, size.width, size.height, (uint32_t)rate.value, bitrate.value};
    return size.width >= 1920 && size.height >= 1080 && camera_encode_dimensions(config);
}

class HostEncoder : public android::SimpleC2Component {
public:
    explicit HostEncoder(const std::shared_ptr<C2ComponentInterface> &intf) : SimpleC2Component(intf) {}
    ~HostEncoder() override { disconnect(); }

    bool connect() {
        if (fd >= 0) return true;
        if (!profile(intf(), &config)) return false;
        sockaddr_un address = {.sun_family = AF_UNIX};
        static_assert(sizeof(socketPath) <= sizeof(address.sun_path));
        std::memcpy(address.sun_path, socketPath, sizeof(socketPath));
        int candidate = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (candidate < 0) return false;
        timeval timeout = {.tv_sec = 3};
        setsockopt(candidate, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        setsockopt(candidate, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        camera_encode_format format = {};
        if (::connect(candidate, (sockaddr *)&address, sizeof address) ||
            video_write(candidate, &config, sizeof config) || video_read(candidate, &format, sizeof format) ||
            format.status != VIDEO_OK) {
            std::fprintf(stderr, "camera encoder: relay unavailable for %ux%u at %u fps\n",
                         config.width, config.height, config.fps);
            close(candidate);
            return false;
        }
        fd = candidate;
        input.resize((size_t)config.width * config.height * 3 / 2);
        __android_log_print(ANDROID_LOG_INFO, logTag, "using %.*s at %ux%u %u fps",
                            (int)sizeof format.encoder, format.encoder, config.width, config.height, config.fps);
        return true;
    }

    void disconnect() {
        if (fd >= 0) close(fd);
        fd = -1;
        pending.clear();
        initData.clear();
        input.clear();
    }

protected:
    c2_status_t onInit() override { return connect() ? C2_OK : C2_NO_INIT; }
    c2_status_t onStop() override { disconnect(); return C2_OK; }
    void onReset() override { disconnect(); }
    void onRelease() override { disconnect(); }
    c2_status_t onFlush_sm() override { disconnect(); return C2_OK; }
    c2_status_t drain(uint32_t mode, const std::shared_ptr<C2BlockPool> &) override {
        return mode == NO_DRAIN ? C2_OK : C2_OMITTED;
    }

    void process(const std::unique_ptr<C2Work> &work, const std::shared_ptr<C2BlockPool> &pool) override {
        work->result = C2_OK;
        work->workletsProcessed = 0;
        const bool eos = (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0;
        const bool hasFrame = !work->input.buffers.empty();
        const int64_t timestamp = work->input.ordinal.timestamp.peekll();
        const uint64_t index = work->input.ordinal.frameIndex.peekull();
        if (!hasFrame && !eos) { complete(work, nullptr, false, false); return; }
        if (timestamp < 0 || !connect() || (hasFrame && !copyInput(work->input.buffers.front()))) {
            fail(work, C2_BAD_VALUE);
            return;
        }
        video_packet packet = {.pts_us = timestamp, .type = VIDEO_SUBMIT, .flags = eos ? uint32_t(VIDEO_EOS) : 0u,
                               .size = hasFrame ? (uint32_t)input.size() : 0};
        uint32_t status = VIDEO_AGAIN;
        for (int retry = 0; retry < 50 && status == VIDEO_AGAIN; ++retry) {
            if (video_write(fd, &packet, sizeof packet) || video_read(fd, &status, sizeof status)) {
                fail(work, C2_CORRUPTED);
                return;
            }
            if (status == VIDEO_OK) {
                if (video_write(fd, input.data(), packet.size) || video_read(fd, &status, sizeof status)) {
                    fail(work, C2_CORRUPTED);
                    return;
                }
                break;
            }
            bool ended = false;
            if (status != VIDEO_AGAIN || receive(work, pool, &ended) == VIDEO_ERROR || ended) {
                fail(work, C2_CORRUPTED);
                return;
            }
        }
        if (status != VIDEO_OK) { fail(work, C2_TIMED_OUT); return; }
        if (hasFrame) pending.emplace(timestamp, index);
        bool ended = false;
        for (int retry = 0; retry < 500 && !ended; ++retry) {
            status = receive(work, pool, &ended);
            if (status == VIDEO_ERROR) { fail(work, C2_CORRUPTED); return; }
            if (!eos && status == VIDEO_AGAIN) break;
        }
        if (eos && (!ended || !pending.empty())) { fail(work, C2_TIMED_OUT); return; }
        if (eos && !work->workletsProcessed) complete(work, nullptr, true, false);
    }

private:
    int fd = -1;
    camera_encode_open config = {};
    std::multimap<int64_t, uint64_t> pending;
    std::vector<uint8_t> input;
    std::vector<uint8_t> initData;

    bool copyInput(const std::shared_ptr<C2Buffer> &buffer) {
        if (!buffer) return false;
        const auto blocks = buffer->data().graphicBlocks();
        if (blocks.size() != 1) return false;
        C2GraphicView view = blocks.front().map().get();
        const auto &layout = view.layout();
        if (view.error() != C2_OK || layout.type != C2PlanarLayout::TYPE_YUV ||
            layout.numPlanes != 3 || view.width() < config.width || view.height() < config.height) {
            std::fprintf(stderr, "camera encoder: graphic input mapping status %d, type %u, planes %u, %ux%u\n",
                         view.error(), (unsigned)layout.type, layout.numPlanes, view.width(), view.height());
            return false;
        }
        const auto &y = layout.planes[C2PlanarLayout::PLANE_Y];
        const auto &u = layout.planes[C2PlanarLayout::PLANE_U];
        const auto &v = layout.planes[C2PlanarLayout::PLANE_V];
        if (y.allocatedDepth != 8 || u.allocatedDepth != 8 || v.allocatedDepth != 8 ||
            y.colInc != 1 || u.colInc < 1 || v.colInc < 1 || y.rowInc < (int32_t)config.width ||
            u.rowInc < (int32_t)(config.width / 2 - 1) * u.colInc + 1 ||
            v.rowInc < (int32_t)(config.width / 2 - 1) * v.colInc + 1) return false;
        const uint8_t *py = view.data()[C2PlanarLayout::PLANE_Y];
        const uint8_t *pu = view.data()[C2PlanarLayout::PLANE_U];
        const uint8_t *pv = view.data()[C2PlanarLayout::PLANE_V];
        if (!py || !pu || !pv) return false;
        uint8_t *out = input.data();
        for (uint32_t row = 0; row < config.height; ++row)
            std::memcpy(out + row * config.width, py + row * y.rowInc, config.width);
        out += config.width * config.height;
        for (uint32_t row = 0; row < config.height / 2; ++row) {
            const uint8_t *su = pu + row * u.rowInc;
            const uint8_t *sv = pv + row * v.rowInc;
            uint32_t x = 0;
            if (u.colInc == 1 && v.colInc == 1) {
                for (; x + 16 <= config.width / 2; x += 16) {
                    const uint8x16x2_t uv = {{vld1q_u8(su + x), vld1q_u8(sv + x)}};
                    vst2q_u8(out + 2 * x, uv);
                }
            }
            for (; x < config.width / 2; ++x) {
                out[2 * x] = su[x * u.colInc];
                out[2 * x + 1] = sv[x * v.colInc];
            }
            out += config.width;
        }
        return true;
    }

    void complete(const std::unique_ptr<C2Work> &work, const std::shared_ptr<C2Buffer> &sample,
                  bool eos, bool sync) {
        auto &out = work->worklets.front()->output;
        out.flags = eos ? C2FrameData::FLAG_END_OF_STREAM : (C2FrameData::flags_t)0;
        out.ordinal = work->input.ordinal;
        out.buffers.clear();
        if (!initData.empty()) {
            auto csd = C2StreamInitDataInfo::output::AllocUnique(initData.size(), 0u);
            if (csd) {
                std::memcpy(csd->m.value, initData.data(), initData.size());
                out.configUpdate.push_back(std::move(csd));
            }
            initData.clear();
        }
        if (sample) {
            if (sync) {
                sample->setInfo(std::make_shared<C2StreamPictureTypeMaskInfo::output>(0u, C2Config::SYNC_FRAME));
            }
            out.buffers.push_back(sample);
        }
        work->workletsProcessed = 1;
    }

    void fail(const std::unique_ptr<C2Work> &work, c2_status_t status) {
        std::fprintf(stderr, "camera encoder: processing error %d, %zu pending frames\n", status, pending.size());
        __android_log_print(ANDROID_LOG_ERROR, logTag, "hardware encoder failed (%d)", status);
        complete(work, nullptr, false, false);
        work->result = status;
        disconnect();
    }

    uint32_t receive(const std::unique_ptr<C2Work> &current, const std::shared_ptr<C2BlockPool> &pool, bool *eos) {
        video_packet query = {.type = VIDEO_RECEIVE};
        video_packet response = {};
        if (video_write(fd, &query, sizeof query) || video_read(fd, &response, sizeof response) ||
            response.size > RME_VIDEO_MAX_SAMPLE) return VIDEO_ERROR;
        if (response.type == VIDEO_AGAIN) return VIDEO_AGAIN;
        if (response.type != VIDEO_OK) return VIDEO_ERROR;
        *eos = (response.flags & VIDEO_EOS) != 0;
        if (!response.size) return VIDEO_OK;
        if (response.flags & VIDEO_CONFIG) {
            if (response.size > RME_VIDEO_MAX_CONFIG) return VIDEO_ERROR;
            initData.resize(response.size);
            return video_read(fd, initData.data(), response.size) ? VIDEO_ERROR : VIDEO_OK;
        }
        auto entry = pending.find(response.pts_us);
        if (entry == pending.end()) {
            std::fprintf(stderr, "camera encoder: missing submitted timestamp %lld\n", (long long)response.pts_us);
            return VIDEO_ERROR;
        }
        uint64_t index = entry->second;
        pending.erase(entry);
        std::shared_ptr<C2LinearBlock> block;
        if (pool->fetchLinearBlock(response.size, {C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block) != C2_OK)
            return VIDEO_ERROR;
        C2WriteView view = block->map().get();
        if (view.error() != C2_OK || view.capacity() < response.size ||
            video_read(fd, view.data(), response.size)) return VIDEO_ERROR;
        auto sample = createLinearBuffer(block, 0, response.size);
        const bool sync = (response.flags & VIDEO_SYNC) != 0;
        if (index == current->input.ordinal.frameIndex.peekull()) complete(current, sample, *eos, sync);
        else finish(index, [this, sample, sync](const std::unique_ptr<C2Work> &work) {
            complete(work, sample, false, sync);
        });
        return VIDEO_OK;
    }
};

// Keep the stock component and parameter interface available for ordinary
// recordings and for hosts that cannot start the requested hardware codec.
class Encoder final : public HostEncoder {
public:
    explicit Encoder(std::shared_ptr<C2Component> original)
        : HostEncoder(original->intf()), software(std::move(original)) {}

    c2_status_t setListener_vb(const std::shared_ptr<Listener> &listener, c2_blocking_t mayBlock) override {
        c2_status_t status = software->setListener_vb(listener, mayBlock);
        return status == C2_OK ? HostEncoder::setListener_vb(listener, mayBlock) : status;
    }
    c2_status_t queue_nb(std::list<std::unique_ptr<C2Work>> *const items) override {
        if (offload) return HostEncoder::queue_nb(items);
        const c2_status_t status = prepareSoftwarePool();
        return status == C2_OK ? software->queue_nb(items) : status;
    }
    c2_status_t announce_nb(const std::vector<C2WorkOutline> &items) override {
        return offload ? HostEncoder::announce_nb(items) : software->announce_nb(items);
    }
    c2_status_t flush_sm(flush_mode_t mode, std::list<std::unique_ptr<C2Work>> *const flushed) override {
        return offload ? HostEncoder::flush_sm(mode, flushed) : software->flush_sm(mode, flushed);
    }
    c2_status_t drain_nb(drain_mode_t mode) override {
        return offload ? HostEncoder::drain_nb(mode) : software->drain_nb(mode);
    }
    c2_status_t start() override {
        // The service registers this component as the owner of its allocated
        // output pool. Run the hardware worker on the same component identity.
        if (connect()) {
            c2_status_t status = HostEncoder::start();
            if (status == C2_OK) { offload = true; return C2_OK; }
            disconnect();
        }
        offload = false;
        {
            std::lock_guard<std::mutex> lock(softwarePoolLock);
            softwarePool.reset();
        }
        return software->start();
    }
    c2_status_t stop() override { return offload ? HostEncoder::stop() : software->stop(); }
    c2_status_t reset() override {
        HostEncoder::reset();
        offload = false;
        return software->reset();
    }
    c2_status_t release() override {
        HostEncoder::release();
        offload = false;
        return software->release();
    }
    std::shared_ptr<C2ComponentInterface> intf() override { return software->intf(); }

private:
    c2_status_t prepareSoftwarePool() {
        std::lock_guard<std::mutex> lock(softwarePoolLock);
        if (softwarePool) return C2_OK;
        // CCodec configures its output pool after start. The stock worker needs
        // a pool registered to its own component identity before its first work.
        std::vector<std::unique_ptr<C2Param>> response;
        const auto interface = software->intf();
        c2_status_t status = interface->query_vb({}, {C2PortBlockPoolsTuning::output::PARAM_TYPE},
                                                 C2_DONT_BLOCK, &response);
        if (status == C2_BAD_INDEX || (status == C2_OK && response.empty())) return C2_OK;
        if (status != C2_OK) return status;
        const auto *configured = C2PortBlockPoolsTuning::output::From(response.front().get());
        if (!configured || configured->flexCount() == 0 || configured->m.values[0] == C2BlockPool::BASIC_LINEAR)
            return C2_OK;
        std::shared_ptr<C2BlockPool> clientPool;
        status = android::GetCodec2BlockPool(configured->m.values[0], shared_from_this(), &clientPool);
        if (status != C2_OK) return status;
        std::shared_ptr<C2BlockPool> pool;
        status = android::CreateCodec2BlockPool(clientPool->getAllocatorId(), software, &pool);
        if (status != C2_OK) return status;
        auto replacement = C2PortBlockPoolsTuning::output::AllocUnique(1u);
        if (!replacement) return C2_NO_MEMORY;
        replacement->m.values[0] = pool->getLocalId();
        std::vector<std::unique_ptr<C2SettingResult>> failures;
        status = interface->config_vb({replacement.get()}, C2_DONT_BLOCK, &failures);
        if (status == C2_OK) softwarePool = std::move(pool);
        return status;
    }

    const std::shared_ptr<C2Component> software;
    std::atomic_bool offload{false};
    std::mutex softwarePoolLock;
    std::shared_ptr<C2BlockPool> softwarePool;
};

class EncoderFactory final : public C2ComponentFactory {
public:
    EncoderFactory(void *library, C2ComponentFactory *factory, DestroyCodec2FactoryFunc destroy)
        : library(library), original(factory), destroy(destroy) {}
    ~EncoderFactory() override { destroy(original); dlclose(library); }

    c2_status_t createComponent(c2_node_id_t id, std::shared_ptr<C2Component> *component,
                                ComponentDeleter deleter) override {
        std::shared_ptr<C2Component> software;
        const c2_status_t status = original->createComponent(id, &software);
        if (status == C2_OK) *component = std::shared_ptr<C2Component>(new Encoder(std::move(software)), std::move(deleter));
        return status;
    }
    c2_status_t createInterface(c2_node_id_t id, std::shared_ptr<C2ComponentInterface> *intf,
                                InterfaceDeleter deleter) override {
        return original->createInterface(id, intf, std::move(deleter));
    }

private:
    void *library;
    C2ComponentFactory *original;
    DestroyCodec2FactoryFunc destroy;
};

}  // namespace

extern "C" __attribute__((visibility("default"))) C2ComponentFactory *CreateCodec2Factory() {
    void *library = dlopen(originalPath, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library) return nullptr;
    auto create = reinterpret_cast<C2ComponentFactory::CreateCodec2FactoryFunc>(dlsym(library, "CreateCodec2Factory"));
    auto destroy = reinterpret_cast<C2ComponentFactory::DestroyCodec2FactoryFunc>(dlsym(library, "DestroyCodec2Factory"));
    C2ComponentFactory *original = create && destroy ? create() : nullptr;
    if (!original) { dlclose(library); return nullptr; }
    return new EncoderFactory(library, original, destroy);
}

extern "C" __attribute__((visibility("default"))) void DestroyCodec2Factory(C2ComponentFactory *factory) {
    delete factory;
}
