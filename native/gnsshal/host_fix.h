#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct host_fix {
    double latitude, longitude, altitude, accuracy, speed, bearing;
    int64_t elapsed_ns, wall_ms;
    bool has_altitude, has_speed, has_bearing;
};

bool parse_host_fix(const char *record, int64_t now_ns, struct host_fix *fix);

#ifdef __cplusplus
}
#endif
