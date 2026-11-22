#define _POSIX_C_SOURCE 200809L
#include "host_fix.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool number(const char *text, double *value) {
    char *end;
    *value = strtod(text, &end);
    return end != text && *end == '\0' && isfinite(*value);
}

static bool optional_number(const char *text, bool *present, double *value) {
    *present = strcmp(text, "null") != 0;
    return !*present || number(text, value);
}

static bool timestamp(const char *text, int64_t *value) {
    char *end;
    errno = 0;
    *value = strtoll(text, &end, 10);
    return errno == 0 && end != text && *end == '\0' && *value > 0;
}

bool parse_host_fix(const char *record, int64_t now_ns, struct host_fix *fix) {
    char copy[512];
    const size_t length = strlen(record);
    if (length >= sizeof(copy)) return false;
    memcpy(copy, record, length + 1);
    char *cursor = NULL;
    const char *fields[9];
    for (size_t index = 0; index < 9; index++) {
        fields[index] = strtok_r(index == 0 ? copy : NULL, " \t\r\n", &cursor);
        if (fields[index] == NULL) return false;
    }
    if (strcmp(fields[0], "2") != 0 || strtok_r(NULL, " \t\r\n", &cursor)) return false;

    struct host_fix value = {0};
    if (!number(fields[1], &value.latitude) || fabs(value.latitude) > 90 ||
        !number(fields[2], &value.longitude) || fabs(value.longitude) > 180 ||
        !optional_number(fields[3], &value.has_altitude, &value.altitude) ||
        !number(fields[4], &value.accuracy) || value.accuracy < 0 ||
        !timestamp(fields[5], &value.elapsed_ns) || value.elapsed_ns > now_ns ||
        now_ns - value.elapsed_ns > INT64_C(30000000000) ||
        !timestamp(fields[6], &value.wall_ms) ||
        !optional_number(fields[7], &value.has_speed, &value.speed) || value.speed < 0 ||
        !optional_number(fields[8], &value.has_bearing, &value.bearing) ||
        value.bearing < 0 || value.bearing >= 360) return false;
    *fix = value;
    return true;
}
