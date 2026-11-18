// Calls the exact upstream list matcher extracted by verify.py. No GPU access.
#include <cassert>
#include <string>
#include <vector>
struct GpuInfo {
    std::string make, model, device_id, revision_id, version, renderer, os;
};
struct GpuInfoList { std::vector<GpuInfo> infos; };
struct BlacklistEntry {
    const char* make;
    const char* model;
    const char* device_id;
    const char* revision_id;
    const char* version;
    const char* renderer;
    const char* os;
};
#include "matcher.inc"
int main() {
    GpuInfoList list;
    GpuInfo gpu;
    gpu.make = "vendor";
    gpu.device_id = "device";
    gpu.revision_id = "revision-1";
    gpu.version = "driver-2";
    list.infos.push_back(gpu);
    BlacklistEntry blocked{"vendor", nullptr, "device", nullptr, "driver-2", nullptr, nullptr};
    assert(gpuinfo_query_list(&list, &blocked, 1));
    blocked.version = "revision-1";
    assert(!gpuinfo_query_list(&list, &blocked, 1));
    blocked.version = nullptr;
    blocked.revision_id = "revision-1";
    assert(gpuinfo_query_list(&list, &blocked, 1));
    blocked.revision_id = "other-revision";
    assert(!gpuinfo_query_list(&list, &blocked, 1));
}
