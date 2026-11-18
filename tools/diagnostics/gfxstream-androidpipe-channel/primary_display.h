#pragma once
#include <cstdint>
struct QAndroidMultiDisplayAgent;
struct QAndroidEmulatorWindowAgent;
namespace rme {
// PixelFold is named explicitly so unsupported topology cannot be silently
// treated as a single primary display.
enum class DisplayTopology { SinglePrimary, PixelFold };
enum class FoldMode { Unknown, NonFoldable, Foldable };
// Call with the live VmLifecycle generation and VmSettings width/height/density.
// This only supplies dimensions; all unrelated agent callbacks stay absent.
// The nonzero returned token identifies this binding across different VMs,
// whose VmLifecycle generation counters can otherwise have identical values.
uint64_t bindPrimaryDisplay(uint64_t generation, uint32_t width, uint32_t height, uint32_t density,
                            DisplayTopology topology, FoldMode foldMode);
bool updatePrimaryDisplay(uint64_t token, uint64_t generation, uint32_t width, uint32_t height, uint32_t density);
bool clearPrimaryDisplay(uint64_t token, uint64_t generation);
// Enables renderer-owned ColorBuffer association callbacks for this binding.
bool enablePrimaryColorBuffers(uint64_t token, uint64_t generation);
bool primaryColorBuffersBound(uint64_t token, uint64_t generation);
// Prevent new lookups and return true only after in-flight callbacks release
// their temporary references. Must precede renderer/FrameBuffer destruction.
bool quiescePrimaryColorBuffers(uint64_t token, uint64_t generation);
const QAndroidMultiDisplayAgent* primaryDisplayAgent();
const QAndroidEmulatorWindowAgent* primaryWindowAgent();
}
