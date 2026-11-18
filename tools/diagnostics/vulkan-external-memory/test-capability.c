// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <vulkan/vulkan.h>
#include "capability.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; \
} } while (0)

int main(void) {
    const uint32_t h = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    const uint32_t other = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    const uint32_t import = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
    const uint32_t export = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;
    CHECK(external_memory_matches(1, import | export, h, h, import | export));
    CHECK(external_memory_matches(1, import, h | other, h, import));
    CHECK(!external_memory_matches(0, import | export, h, h, import | export));
    CHECK(!external_memory_matches(1, import, h, h, import | export));
    CHECK(!external_memory_matches(1, export, h, h, import | export));
    CHECK(!external_memory_matches(1, import | export, other, h, import | export));
    CHECK(!external_memory_matches(1, import | export, h, 0, import));
    CHECK(!external_memory_matches(1, import | export, h | other, h | other, import));
    const uint32_t host = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
    const HandleProperties all_compatible[] = {
        { h, export, h | other, QUERY_OK }, { other, export, h | other, QUERY_OK }
    };
    CHECK(external_combination(all_compatible, 2, h | other, export) == COMBINATION_SUPPORTED);
    const HandleProperties mismatch[] = {
        { h, export, h, QUERY_OK }, { other, export, other, QUERY_OK }
    };
    CHECK(external_combination(mismatch, 2, h | other, export) == COMBINATION_UNSUPPORTED);
    const HandleProperties missing_export[] = {
        { host, import, host | other, QUERY_OK }, { other, export, host | other, QUERY_OK }
    };
    CHECK(external_combination(missing_export, 2, host | other, export) == COMBINATION_UNSUPPORTED);
    const HandleProperties unavailable[] = {
        { host, export, host | other, QUERY_OK }, { other, 0, 0, QUERY_UNKNOWN }
    };
    CHECK(external_combination(unavailable, 2, host | other, export) == COMBINATION_UNKNOWN);
    const HandleProperties format_unsupported[] = {
        { host, export, host | other, QUERY_OK }, { other, 0, 0, QUERY_UNSUPPORTED }
    };
    CHECK(external_combination(format_unsupported, 2, host | other, export) == COMBINATION_UNSUPPORTED);
    CHECK(external_combination(all_compatible, 1, h | other, export) == COMBINATION_UNKNOWN);
    CHECK(external_combination(all_compatible, 2, h | other | host, export) == COMBINATION_UNKNOWN);
    const HandleProperties duplicate[] = {
        { h, export, h | other, QUERY_OK }, { h, export, h | other, QUERY_OK }
    };
    CHECK(external_combination(duplicate, 2, h | other, export) == COMBINATION_UNKNOWN);
    const HandleProperties unknown_and_bad[] = {
        { h, 0, h, QUERY_OK }, { other, 0, 0, QUERY_UNKNOWN }
    };
    CHECK(external_combination(unknown_and_bad, 2, h | other, export) == COMBINATION_UNKNOWN);
    puts("CAPABILITY_TEST_OK");
    return 0;
}
