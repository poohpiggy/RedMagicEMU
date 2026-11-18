#include <cassert>
#include <cstdio>
static bool sOpenglLoggerInitialized = false, sRendererUsesSubWindow = false;
static void* sRenderer = nullptr;
#define AEMU_EXPORT
#include "headless_config.inc"
int main() {
    assert(rme_set_renderer_headless() != 0 && !sRendererUsesSubWindow);
    // prepare() explicitly keeps the existing subwindow mode as its default.
    sOpenglLoggerInitialized = true;
    sRendererUsesSubWindow = true;
    assert(rme_set_renderer_headless() == 0 && !sRendererUsesSubWindow);
    sRendererUsesSubWindow = true;
    sRenderer = reinterpret_cast<void*>(1);
    assert(rme_set_renderer_headless() != 0 && sRendererUsesSubWindow);
    std::puts("HEADLESS_CONFIGURATION_GUARDS_AND_DEFAULT_OK");
}
