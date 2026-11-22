#include "../host_fix.h"

#include <assert.h>

int main(void) {
    const int64_t now = 100000000000;
    struct host_fix fix;
    assert(parse_host_fix("2 12.25 -34.5 56.75 4.5 99000000000 1790000000000 2.5 133.75\n", now, &fix));
    assert(fix.latitude == 12.25 && fix.longitude == -34.5);
    assert(fix.has_altitude && fix.altitude == 56.75 && fix.accuracy == 4.5);
    assert(fix.elapsed_ns == 99000000000 && fix.wall_ms == 1790000000000);
    assert(fix.has_speed && fix.speed == 2.5 && fix.has_bearing && fix.bearing == 133.75);

    assert(parse_host_fix("2 12.25 -34.5 null 4.5 99000000000 1790000000000 null null\n", now, &fix));
    assert(!fix.has_altitude && !fix.has_speed && !fix.has_bearing);
    assert(!parse_host_fix("1 12.25 -34.5 null 4.5 99000000000 1790000000000\n", now, &fix));
    assert(!parse_host_fix("2 12.25 -34.5 null 4.5 69000000000 1790000000000 null null\n", now, &fix));
    assert(!parse_host_fix("2 12.25 -34.5 null 4.5 101000000000 1790000000000 null null\n", now, &fix));
    assert(!parse_host_fix("2 95 -34.5 null 4.5 99000000000 1790000000000 null null\n", now, &fix));
    assert(!parse_host_fix("2 12.25 -34.5 null 4.5 99000000000 1790000000000 -1 null\n", now, &fix));
    assert(!parse_host_fix("2 12.25 -34.5 null 4.5 99000000000 1790000000000 null 360\n", now, &fix));
    assert(!parse_host_fix("2 12.25 -34.5 null 4.5 99000000000 1790000000000 null null junk\n", now, &fix));
}
