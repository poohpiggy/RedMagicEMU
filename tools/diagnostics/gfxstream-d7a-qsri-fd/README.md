# QSRI sync-fd research: do not integrate

This directory contains an unfinished offline experiment against the locally
examined gfxstream `d7a33801fb557cebd4f8564a8af431669b3f471d` source. The
candidate `fix.patch` is **not approved for integration or packaging**.
Its `VK_ERROR_UNKNOWN` result may propagate into Android's present-result
aggregation and trigger a conditional assertion. The actual target firmware's
version and build configuration have not been verified.
In the native-sync path, export failure can also occur after an asynchronous
command was already submitted; the candidate does not remove that side effect.

The test harness uses fake handles and a controlled local-variable initialization
mode. Its observed fake close value cannot establish the value or behavior on an
Android device. The candidate has not been tested with a real fence, actual
Goldfish sync, a complete Vulkan client, or a Guest.

Preserve all upstream source copyright and license notices. The sources and the
local build dependencies are not included here. Do not treat the existence of
this experiment as proof that QSRI or Guest Vulkan is functional.
