#!/usr/bin/env python3
"""Exercise actual generated create request/response with the host adapter."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument("generated", type=Path)
p.add_argument("gfxstream", type=Path)
p.add_argument("ndk", type=Path)
args = p.parse_args()
guest = (args.generated / "guest/VkEncoder.cpp").read_text()
host = (args.generated / "host/VkDecoder.cpp").read_text()
g0 = guest.index("VkEncoder::vkCreateEmuSurfaceRME(")
g1 = guest.index("\n}\n", g0)
g = guest[g0:g1]
h0 = host.index("case OP_vkCreateEmuSurfaceRME:")
h1 = host.index("case OP_", h0 + 8)
h = host[h0:h1]
def part(s, start, end):
    a = s.index(start)
    return s[a:s.index(end, a)]
g_request = part(g, "  uint64_t cgen_var_0;\n  *&cgen_var_0 = get_host_u64_VkInstance", "  if (watchdog) {")
g_response = part(g, "  uint64_t cgen_var_2;", "  ++encodeCount;")
h_request = part(h, "      uint64_t cgen_var_0;", "      if (m_logCalls) {")
h_call = part(h, "      VkResult vkCreateEmuSurfaceRME_VkResult_return", "      if ((vkCreateEmuSurfaceRME_VkResult_return)")
h_response = part(h, "      vkStream->unsetHandleMapping();", "      vkStream->commitWrite();")
gated = "m_rmeWsiVersion" in h_call
program = r'''
#include "host_guest_surface.h"
#include "surface_jni.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
struct ANativeWindow { int refs; };
static ANativeWindow localWindow;
extern "C" ANativeWindow* ANativeWindow_fromSurface(JNIEnv*, jobject) { ++localWindow.refs; return &localWindow; }
extern "C" void ANativeWindow_acquire(ANativeWindow* w) { ++w->refs; }
extern "C" void ANativeWindow_release(ANativeWindow* w) { assert(w->refs > 0); --w->refs; }
static VkInstance hostInstance = (VkInstance)(uintptr_t)0x1133;
static VkInstance boxedInstance = (VkInstance)(uintptr_t)0x2244;
static VkSurfaceKHR rawSurface = (VkSurfaceKHR)(uintptr_t)0x5566;
static const uint64_t boxedSurface = 0x7788;
static VkSurfaceKHR guestSurface = (VkSurfaceKHR)(uintptr_t)0x99aa;
static int creates, destroys, mappingCreates;
static VKAPI_ATTR VkResult VKAPI_CALL driverCreate(VkInstance i,const VkAndroidSurfaceCreateInfoKHR* info,
                                                   const VkAllocationCallbacks* alloc,VkSurfaceKHR* out) {
    assert(i == hostInstance && info->window == &localWindow && !alloc); ++creates; *out = rawSurface; return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL driverDestroy(VkInstance i,VkSurfaceKHR s,const VkAllocationCallbacks* a) {
    assert(i == hostInstance && s == rawSurface && !a); ++destroys;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL getProc(VkInstance i,const char* name) {
    assert(i == hostInstance);
    if (!strcmp(name,"vkCreateAndroidSurfaceKHR")) return (PFN_vkVoidFunction)driverCreate;
    if (!strcmp(name,"vkDestroySurfaceKHR")) return (PFN_vkVoidFunction)driverDestroy;
    return nullptr;
}
struct Dispatch { PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = getProc; } dispatch;
static VkInstance unbox_VkInstance(VkInstance i) { assert(i == boxedInstance); return hostInstance; }
static Dispatch* dispatch_VkInstance(VkInstance i) { assert(i == boxedInstance); return &dispatch; }
static uint64_t get_host_u64_VkInstance(VkInstance i) { assert(i == hostInstance); return (uint64_t)(uintptr_t)boxedInstance; }
struct Mapping {
    bool create = false;
    void mapHandles_VkSurfaceKHR_u64(VkSurfaceKHR* p,uint64_t* out,int) { *out = create ? boxedSurface : (uint64_t)(uintptr_t)*p; }
    void mapHandles_u64_VkSurfaceKHR(uint64_t* in,VkSurfaceKHR* out,int) { assert(create && *in == boxedSurface); ++mappingCreates; *out = guestSurface; }
} m_boxedHandleCreateMapping{true}, m_boxedHandleUnwrapMapping{}, defaultMapping{}, guestMapping{true};
struct Tracker { Mapping* createMapping() { return &guestMapping; } } tracker;
static Tracker* sResourceTracker = &tracker;
struct Wire {
    uint8_t data[128]{}; size_t size = 0, pos = 0; Mapping* mapping = &defaultMapping;
    void write(const void* p,size_t n) { assert(size+n <= sizeof(data)); memcpy(data+size,p,n); size+=n; }
    void read(void* p,size_t n) { assert(pos+n <= size); memcpy(p,data+pos,n); pos+=n; }
    void unsetHandleMapping() { mapping = &defaultMapping; }
    void setHandleMapping(Mapping* m) { mapping = m; }
    Mapping* handleMapping() { return mapping; }
};
struct ReadStream {
    VkSurfaceKHR storage{};
    void unsetHandleMapping() {}
    void alloc(void** p,size_t n) { assert(n == sizeof(storage)); *p = &storage; }
};
static void makeRequest(uint8_t* bytes,VkSurfaceKHR* pSurface) {
    VkInstance local_instance = hostInstance;
    uint8_t* streamPtr = bytes; uint8_t** streamPtrPtr = &streamPtr;
''' + g_request + r'''
    assert(streamPtr-bytes == 16 && (uintptr_t)*pSurface == 0xdeadbeef);
}
static void decode(uint8_t* bytes,Wire* vkStream,uint32_t m_rmeWsiVersion) {
    uint8_t* read = bytes; uint8_t** readStreamPtrPtr = &read;
    VkInstance instance; VkSurfaceKHR* pSurface;
    ReadStream readState; ReadStream* vkReadStream = &readState;
    (void)m_rmeWsiVersion;
''' + h_request + h_call + h_response + r'''
}
static VkResult readReply(Wire* stream,VkSurfaceKHR* pSurface) {
''' + g_response + r'''
    return vkCreateEmuSurfaceRME_VkResult_return;
}
int main() {
    JNIEnv* env = (JNIEnv*)(uintptr_t)1;
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env,nullptr,(jobject)(uintptr_t)1));
    uint8_t request[16]{}; VkSurfaceKHR output=(VkSurfaceKHR)(uintptr_t)0xdeadbeef;
    makeRequest(request,&output);
    uint64_t sent; memcpy(&sent,request+8,8); assert(sent == 0);
''' + (r'''
    Wire unnegotiated; decode(request,&unnegotiated,0);
    assert(readReply(&unnegotiated,&output) == VK_ERROR_EXTENSION_NOT_PRESENT && !output);
    assert(creates == 0 && localWindow.refs == 1 && mappingCreates == 0);
''' if gated else '') + r'''
    Wire response; decode(request,&response,1);
    assert(creates == 1 && localWindow.refs == 2);
    uint64_t boxed; memcpy(&boxed,response.data,8); assert(boxed == boxedSurface);
    assert(readReply(&response,&output) == VK_SUCCESS && output == guestSurface && mappingCreates == 1);
    assert(rme_vk_guest_surface_destroy(hostInstance,rawSurface) && destroys == 1 && localWindow.refs == 1);
    assert(Java_app_aemu_core_VulkanSurface_nativeUpdate(env,nullptr,nullptr));
    Wire failed; decode(request,&failed,1); output=(VkSurfaceKHR)(uintptr_t)0xdeadbeef;
    assert(readReply(&failed,&output) == VK_ERROR_SURFACE_LOST_KHR && !output && mappingCreates == 1);
    return 0;
}
'''
here = Path(__file__).resolve().parent
repo = here.parents[2]
native = repo / "native/vulkanbridge"
fake = repo / "tools/diagnostics/gfxstream-android-surface/test_include"
binpath = args.ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
with tempfile.TemporaryDirectory(prefix="rme-surface-create-wire-") as tmp:
    root = Path(tmp)
    cpp = root / "test.cpp"
    cpp.write_text(program)
    includes = ["-I" + str(fake), "-I" + str(args.gfxstream / "src/common/vulkan/include"),
                "-I" + str(native), "-I" + str(here)]
    objs = []
    for source in (here / "host_guest_surface.c", native / "host_surface.c", native / "surface_jni.c"):
        obj = root / (source.stem + ".o")
        subprocess.run([str(binpath / "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-DVK_USE_PLATFORM_ANDROID_KHR", *includes, "-c", str(source), "-o", str(obj)], check=True)
        objs.append(str(obj))
    subprocess.run([str(binpath / "clang++"), "-std=c++17", "-nostdlib++", "-Wall", "-Wextra", "-Werror",
                    "-DVK_USE_PLATFORM_ANDROID_KHR", *includes, str(cpp), *objs, "-pthread", "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PRIVATE_CREATE_WIRE_HOST_WINDOW_INSTANCE_AND_FAILURE_OK")
