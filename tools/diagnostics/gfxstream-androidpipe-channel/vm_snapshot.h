#pragma once
#include <cstdint>
struct QAndroidVmOperations;
namespace rme {
enum class SnapshotCapability { Unknown, Unavailable, Supported };
// Caller asserts the current VM's actual snapshot capability; this component
// only supports explicitly Unavailable. Token and generation identify binding.
uint64_t bindSnapshotVm(uint64_t generation, SnapshotCapability capability);
bool clearSnapshotVm(uint64_t token, uint64_t generation);
bool readSnapshotSkipRequest(uint64_t token, uint64_t generation, bool* requested);
const QAndroidVmOperations* snapshotVmAgent();
}
