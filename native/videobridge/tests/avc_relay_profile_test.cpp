#include "../camera_encode_wire.h"

#include <cassert>

int main() {
    camera_encode_open request{RME_CAMERA_ENCODE_MAGIC, 1920, 1080, 30, 6220800};
    assert(camera_encode_dimensions(&request));

    const unsigned camera_rates[] = {50, 60, 120};
    for (const auto rate : camera_rates) {
        request.fps = rate;
        assert(camera_encode_dimensions(&request));
    }

    request.fps = 0;
    assert(!camera_encode_dimensions(&request));
    request.fps = 30;
    request.width = 1919;
    assert(!camera_encode_dimensions(&request));
}
