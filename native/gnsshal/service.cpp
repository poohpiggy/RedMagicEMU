#include "host_fix.h"

#include <aidl/android/hardware/gnss/BnGnss.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <thread>
#include <time.h>
#include <unistd.h>

using namespace aidl::android::hardware::gnss;
using ndk::ScopedAStatus;

namespace {
constexpr char kInput[] = "/dev/aemu_location";
constexpr char kDemand[] = "/dev/aemu_location_demand";
constexpr char kReady[] = "/dev/.aemu-gnss-ready";
constexpr char kService[] = "android.hardware.gnss.IGnss/default";

int64_t bootNs() {
    timespec time{};
    if (clock_gettime(CLOCK_BOOTTIME, &time) != 0) return 0;
    return time.tv_sec * INT64_C(1000000000) + time.tv_nsec;
}

bool readFix(int64_t now, struct host_fix* fix) {
    const int fd = open(kInput, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    char record[512];
    struct stat stat{};
    const ssize_t size = fstat(fd, &stat) == 0 && S_ISREG(stat.st_mode)
        ? read(fd, record, sizeof(record)) : -1;
    close(fd);
    if (size <= 0 || size >= static_cast<ssize_t>(sizeof(record))) return false;
    record[size] = '\0';
    return parse_host_fix(record, now, fix);
}

bool writeDemand(bool active, int64_t now) {
    char path[] = "/dev/.aemu-gnss-demand-XXXXXX";
    const int fd = mkstemp(path);
    if (fd < 0) return false;
    char record[32];
    const int size = active
        ? snprintf(record, sizeof(record), "1 %lld\n", static_cast<long long>(now / 1000000 + 4000))
        : snprintf(record, sizeof(record), "0\n");
    const bool written = write(fd, record, size) == size;
    close(fd);
    const bool renamed = written && rename(path, kDemand) == 0;
    if (!renamed) unlink(path);
    return renamed;
}

ScopedAStatus unsupported() {
    return ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

GnssLocation toLocation(const struct host_fix& fix) {
    GnssLocation result;
    result.gnssLocationFlags = static_cast<int>(GnssLocation::HAS_LAT_LONG) |
        static_cast<int>(GnssLocation::HAS_HORIZONTAL_ACCURACY);
    result.latitudeDegrees = fix.latitude;
    result.longitudeDegrees = fix.longitude;
    result.horizontalAccuracyMeters = fix.accuracy;
    result.timestampMillis = fix.wall_ms;
    result.elapsedRealtime.flags = ElapsedRealtime::HAS_TIMESTAMP_NS;
    result.elapsedRealtime.timestampNs = fix.elapsed_ns;
    if (fix.has_altitude) {
        result.gnssLocationFlags |= GnssLocation::HAS_ALTITUDE;
        result.altitudeMeters = fix.altitude;
    }
    if (fix.has_speed) {
        result.gnssLocationFlags |= GnssLocation::HAS_SPEED;
        result.speedMetersPerSec = fix.speed;
    }
    if (fix.has_bearing) {
        result.gnssLocationFlags |= GnssLocation::HAS_BEARING;
        result.bearingDegrees = fix.bearing;
    }
    return result;
}

class GnssService final : public BnGnss {
public:
    GnssService() : worker_(&GnssService::run, this) {}
    ~GnssService() override {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        wake_.notify_one();
        worker_.join();
    }

    ScopedAStatus setCallback(const std::shared_ptr<IGnssCallback>& callback) override {
        if (!callback) return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
        auto status = callback->gnssSetCapabilitiesCb(IGnssCallback::CAPABILITY_SCHEDULING);
        if (!status.isOk()) return status;
        IGnssCallback::GnssSystemInfo info;
        info.name = "RedMagicEMU host GNSS";
        status = callback->gnssSetSystemInfoCb(info);
        if (!status.isOk()) return status;
        std::lock_guard lock(mutex_);
        callback_ = callback;
        return ScopedAStatus::ok();
    }

    ScopedAStatus start() override {
        std::lock_guard lock(mutex_);
        if (!callback_) return ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
        if (!active_) {
            const auto now = bootNs();
            if (!now || !writeDemand(true, now)) return ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
            renewed_ = now;
            lastFix_ = 0;
            active_ = true;
            puts("gnss: active request");
            wake_.notify_one();
        }
        return ScopedAStatus::ok();
    }

    ScopedAStatus stop() override {
        std::lock_guard lock(mutex_);
        endRequest();
        return ScopedAStatus::ok();
    }

    ScopedAStatus close() override {
        std::lock_guard lock(mutex_);
        endRequest();
        callback_.reset();
        return ScopedAStatus::ok();
    }

    ScopedAStatus setPositionMode(const PositionModeOptions& options) override {
        std::lock_guard lock(mutex_);
        intervalNs_ = std::max<int32_t>(options.minIntervalMs, 1000) * INT64_C(1000000);
        return ScopedAStatus::ok();
    }

    ScopedAStatus injectTime(int64_t, int64_t, int32_t) override { return unsupported(); }
    ScopedAStatus injectLocation(const GnssLocation&) override { return unsupported(); }
    ScopedAStatus injectBestLocation(const GnssLocation&) override { return unsupported(); }
    ScopedAStatus deleteAidingData(GnssAidingData) override { return unsupported(); }
    ScopedAStatus startSvStatus() override { return unsupported(); }
    ScopedAStatus stopSvStatus() override { return unsupported(); }
    ScopedAStatus startNmea() override { return unsupported(); }
    ScopedAStatus stopNmea() override { return unsupported(); }

    // The target's JNI wraps successful extension results before calling them.
    ScopedAStatus getExtensionPsds(std::shared_ptr<IGnssPsds>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssBatching(std::shared_ptr<IGnssBatching>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssNavigationMessage(std::shared_ptr<IGnssNavigationMessageInterface>*) override { return unsupported(); }
    ScopedAStatus getExtensionMeasurementCorrections(std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssConfiguration(std::shared_ptr<IGnssConfiguration>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssMeasurement(std::shared_ptr<IGnssMeasurementInterface>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssPowerIndication(std::shared_ptr<IGnssPowerIndication>*) override { return unsupported(); }
    ScopedAStatus getExtensionAGnss(std::shared_ptr<IAGnss>*) override { return unsupported(); }
    ScopedAStatus getExtensionAGnssRil(std::shared_ptr<IAGnssRil>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssDebug(std::shared_ptr<IGnssDebug>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssVisibilityControl(std::shared_ptr<visibility_control::IGnssVisibilityControl>*) override { return unsupported(); }
    ScopedAStatus getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo>*) override { return unsupported(); }

private:
    void endRequest() {
        if (!active_) return;
        active_ = false;
        if (!writeDemand(false, 0)) fprintf(stderr, "gnss: could not clear demand lease\n");
        puts("gnss: request ended");
    }

    void run() {
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return stopped_ || active_; });
            if (stopped_) return;
            const auto now = bootNs();
            if (now && now - renewed_ >= INT64_C(1000000000) && writeDemand(true, now)) renewed_ = now;
            struct host_fix fix{};
            if (now && readFix(now, &fix) &&
                (lastFix_ == 0 || fix.elapsed_ns - lastFix_ >= intervalNs_)) {
                lastFix_ = fix.elapsed_ns;
                auto callback = callback_;
                lock.unlock();
                const auto status = callback->gnssLocationCb(toLocation(fix));
                lock.lock();
                if (status.isOk() && !sentFix_) {
                    puts("gnss: forwarded a host satellite fix");
                    sentFix_ = true;
                } else if (!status.isOk() && callback_ == callback) {
                    fprintf(stderr, "gnss: location callback disconnected\n");
                    endRequest();
                }
            }
            wake_.wait_for(lock, std::chrono::milliseconds(250), [this] { return stopped_ || !active_; });
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::shared_ptr<IGnssCallback> callback_;
    bool active_ = false;
    bool stopped_ = false;
    bool sentFix_ = false;
    int64_t renewed_ = 0;
    int64_t intervalNs_ = 1000000000;
    int64_t lastFix_ = 0;
    std::thread worker_;
};
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--probe") == 0) {
        const bool declared = AServiceManager_isDeclared(kService);
        const ndk::SpAIBinder existing(AServiceManager_checkService(kService));
        printf("gnss: declared=%d registered=%d\n", declared, existing.get() != nullptr);
        return 0;
    }
    if (argc != 1) return 2;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    ABinderProcess_setThreadPoolMaxThreadCount(2);
    auto service = ndk::SharedRefBase::make<GnssService>();
    const auto status = AServiceManager_addService(service->asBinder().get(), kService);
    if (status != STATUS_OK) {
        fprintf(stderr, "gnss: binder registration failed (%d)\n", status);
        return 1;
    }
    const int fd = open(kReady, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        perror("gnss: cannot signal readiness");
        return 1;
    }
    close(fd);
    puts("gnss: registered GNSS AIDL service");
    ABinderProcess_joinThreadPool();
    return 1;
}
