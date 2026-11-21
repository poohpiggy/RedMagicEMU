#ifndef RME_CAMERA_RAW_H
#define RME_CAMERA_RAW_H

#include <stdint.h>

#define FORMAT_RAW_SENSOR 0x20
#define RAW_BYTE_LIMIT (64 * 1024 * 1024)

/* Little-endian RAW profile and per-shot calibration written by TargetCameraRaw. */
struct raw_profile {
    int32_t width, height;
    int64_t min_duration, stall_duration;
    int32_t active[4], precorrection[4], pixels[2], black[4];
    int32_t white, analog_iso, cfa, illuminant1, illuminant2;
    float aperture, focal;
    int32_t matrices[6][18];
    uint32_t version;
};

struct raw_result {
    int64_t timestamp;
    float aperture, focal;
    int32_t boost, white;
    float black[4];
    int32_t neutral[6];
};

_Static_assert(sizeof(struct raw_profile) == 544, "RAW profile wire size");
_Static_assert(sizeof(struct raw_result) == 64, "RAW result wire size");

static int raw_profile_valid(const struct raw_profile *p) {
    return p->version == 1 && p->width > 0 && p->width % 16 == 0 &&
           p->height > 0 && p->height % 2 == 0 &&
           (int64_t)p->width * p->height * 2 <= RAW_BYTE_LIMIT &&
           p->active[2] > 0 && p->active[3] > 0 &&
           ((p->width == p->pixels[0] && p->height == p->pixels[1]) ||
            (p->width == p->precorrection[2] && p->height == p->precorrection[3])) &&
           p->white > 0 && p->analog_iso > 0 && p->aperture > 0 && p->focal > 0 &&
           p->cfa >= 0 && p->cfa <= 3 && p->illuminant1 > 0;
}

#endif
