#include <cstdio>
#include <stdexcept>

using EGLNativeWindowType = void*;
using FBNativeWindowType = void*;
static int releases = 0, errors = 0;
static void destroySubWindow(void* p) { if (!p) throw std::runtime_error("release null"); ++releases; }
struct Fatal {
    template<class T> Fatal& operator<<(const T&) { throw std::runtime_error("upstream abort"); }
};
#define GFXSTREAM_ABORT(...) Fatal()
#define ERR(...) (++errors)

struct FrameBuffer {
    void* m_emulationGl = nullptr;
    void* m_displayVk = reinterpret_cast<void*>(4);
    void* m_subWin = reinterpret_cast<void*>(1);
    void* m_nativeWindow = reinterpret_cast<void*>(2);
    bool failurePath() {
#include "framebuffer_failure.inc"
        // The surrounding real setupSubWindow keeps its false success flag.
        return false;
    }
};
int main() {
    FrameBuffer vulkan;
    try {
        if (vulkan.failurePath() || vulkan.m_subWin || vulkan.m_nativeWindow || releases != 1 || errors != 1)
            return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "VULKAN_SURFACE_FAILURE: %s\n", e.what());
        return 1;
    }
    FrameBuffer gles;
    gles.m_emulationGl = reinterpret_cast<void*>(3);
    gles.m_displayVk = nullptr;
    if (gles.failurePath() || gles.m_subWin || releases != 2 || errors != 1) return 1;
    FrameBuffer mixed;
    mixed.m_emulationGl = reinterpret_cast<void*>(3);
    if (mixed.failurePath() || mixed.m_subWin || mixed.m_nativeWindow || releases != 3 || errors != 2) return 1;
    std::puts("VULKAN_SURFACE_FAILURE_RELEASES_WINDOW_OK_GLES_UNCHANGED");
}
