#pragma once
#include <cstdint>
#include <memory>
namespace rme {
// Looks up a gfxstream ColorBuffer in the current renderer's registry.
// This is not a Guest gralloc handle, and says nothing about image sharing.
std::shared_ptr<void> findRegisteredColorBuffer(uint32_t handle);
}
