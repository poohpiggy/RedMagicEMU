#ifndef AEMU_BINDER_HW_GET_TRACE_H
#define AEMU_BINDER_HW_GET_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { HWGET_UNAVAILABLE, HWGET_OTHER, HWGET_C2, HWGET_OMX, HWGET_NOT_MANAGER };
enum { HWINST_UNAVAILABLE, HWINST_OTHER, HWINST_DEFAULT };
struct hwget_class { int service, instance; };

/* Compare only the two allowlisted services; never return any Parcel bytes. */
static struct hwget_class hwget_classify(int enabled, int inline_ok, const void *data,
        size_t received, size_t data_size, size_t offsets_size, size_t extra_size) {
    static const char token[] = "android.hidl.manager@1.0::IServiceManager";
    static const char c2[] = "android.hardware.media.c2@1.0::IComponentStore";
    static const char omx[] = "android.hardware.media.omx@1.0::IOmxStore";
    static const char def[] = "default";
    struct hwget_class result = { HWGET_UNAVAILABLE, HWINST_UNAVAILABLE };
    if (!enabled || !inline_ok || !data || data_size > SIZE_MAX - 7) return result;
    size_t aligned = (data_size + 7) & ~(size_t)7;
    if (aligned > received || offsets_size != 4 * sizeof(uint64_t) ||
        offsets_size > received - aligned || extra_size > received - aligned - offsets_size ||
        data_size < sizeof token) return result;
    const unsigned char *p = data;
    if (p[sizeof token - 1] != 0 || memchr(p, 0, sizeof token - 1)) return result;
    if (memcmp(p, token, sizeof token - 1)) {
        result.service = HWGET_NOT_MANAGER;
        return result;
    }
    uint64_t offs[4], lengths[4];
    size_t pos[4], cursor = aligned + offsets_size, extra_end = cursor + extra_size;
    memcpy(offs, p + aligned, sizeof offs);
    for (size_t i = 0; i < 4; i++) {
        if (offs[i] > SIZE_MAX || offs[i] < sizeof token || offs[i] > data_size ||
            data_size - (size_t)offs[i] < 40 ||
            (i && (offs[i] < offs[i - 1] || offs[i] - offs[i - 1] < 40))) return result;
        uint32_t type, flags;
        uint64_t parent, parent_offset;
        const unsigned char *o = p + (size_t)offs[i];
        memcpy(&type, o, 4); memcpy(&flags, o + 4, 4);
        memcpy(&lengths[i], o + 16, 8); memcpy(&parent, o + 24, 8); memcpy(&parent_offset, o + 32, 8);
        if (type != 0x70742a85u || flags != (i % 2 ? 1u : 0u) ||
            (i % 2 && (parent != i - 1 || parent_offset != 0)) ||
            (i % 2 == 0 && lengths[i] != 16) ||
            (i % 2 && (lengths[i] == 0 || lengths[i] > 257))) return result;
        if (lengths[i] > SIZE_MAX - 7) return result;
        size_t size = ((size_t)lengths[i] + 7) & ~(size_t)7;
        if (cursor > extra_end || size > extra_end - cursor) return result;
        pos[i] = cursor; cursor += size;
    }
    if (cursor != extra_end) return result;
    for (size_t i = 0; i < 4; i += 2) {
        uint32_t count;
        memcpy(&count, p + pos[i] + 8, 4);
        if ((uint64_t)count + 1 != lengths[i + 1] ||
            p[pos[i + 1] + count] != 0 || memchr(p + pos[i + 1], 0, count)) return result;
    }
    size_t service_len = (size_t)lengths[1] - 1, instance_len = (size_t)lengths[3] - 1;
    result.service = service_len == sizeof c2 - 1 && !memcmp(p + pos[1], c2, service_len) ? HWGET_C2 :
                     service_len == sizeof omx - 1 && !memcmp(p + pos[1], omx, service_len) ? HWGET_OMX : HWGET_OTHER;
    result.instance = instance_len == sizeof def - 1 && !memcmp(p + pos[3], def, instance_len) ? HWINST_DEFAULT : HWINST_OTHER;
    return result;
}
#endif
