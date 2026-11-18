#include "vm_snapshot.h"
#include "host-common/vm_operations.h"
#include <mutex>

namespace rme {
namespace {
std::mutex mutex;
uint64_t currentGeneration = 0, currentToken = 0, nextToken = 0;
bool requestedSkip = false;
void setSkipSnapshotSave(bool skip) {
    std::lock_guard<std::mutex> guard(mutex);
    if (currentGeneration) requestedSkip = skip;
}
bool isSnapshotSaveSkipped() {
    // All supported bindings explicitly declare snapshot capability absent.
    // No binding also fails closed; it never claims snapshot support.
    return true;
}
}
uint64_t bindSnapshotVm(uint64_t generation, SnapshotCapability capability) {
    if (!generation || capability != SnapshotCapability::Unavailable) return 0;
    std::lock_guard<std::mutex> guard(mutex);
    if (currentGeneration || nextToken == UINT64_MAX) return 0;
    currentGeneration = generation;
    currentToken = ++nextToken;
    requestedSkip = false;
    return currentToken;
}
bool clearSnapshotVm(uint64_t token, uint64_t generation) {
    std::lock_guard<std::mutex> guard(mutex);
    if (!token || !generation || currentToken != token || currentGeneration != generation) return false;
    currentToken = currentGeneration = 0;
    requestedSkip = false;
    return true;
}
bool readSnapshotSkipRequest(uint64_t token, uint64_t generation, bool* requested) {
    if (!requested) return false;
    std::lock_guard<std::mutex> guard(mutex);
    if (!token || !generation || currentToken != token || currentGeneration != generation) return false;
    *requested = requestedSkip;
    return true;
}
const QAndroidVmOperations* snapshotVmAgent() {
    static const QAndroidVmOperations agent = [] {
        QAndroidVmOperations value{};
        value.setSkipSnapshotSave = setSkipSnapshotSave;
        value.isSnapshotSaveSkipped = isSnapshotSaveSkipped;
        return value;
    }();
    std::lock_guard<std::mutex> guard(mutex);
    return currentGeneration ? &agent : nullptr;
}
}
