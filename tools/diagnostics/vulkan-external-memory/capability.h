// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

static inline int external_memory_matches(int query_succeeded, uint32_t features,
                                          uint32_t compatible, uint32_t handle,
                                          uint32_t required_features) {
    return query_succeeded && handle && !(handle & (handle - 1)) &&
           (compatible & handle) == handle &&
           (features & required_features) == required_features;
}

typedef enum { QUERY_UNKNOWN, QUERY_OK, QUERY_UNSUPPORTED } QueryState;
typedef enum { COMBINATION_UNKNOWN, COMBINATION_UNSUPPORTED, COMBINATION_SUPPORTED } CombinationState;
typedef struct {
    uint32_t handle;
    uint32_t features;
    uint32_t compatible;
    QueryState query;
} HandleProperties;

static inline CombinationState external_combination(const HandleProperties *members, uint32_t count,
                                                     uint32_t combination, uint32_t exportable) {
    if (!members || !count || !combination || !exportable) return COMBINATION_UNKNOWN;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t h = members[i].handle;
        if (!h || (h & (h - 1)) || !(combination & h) || (seen & h))
            return COMBINATION_UNKNOWN;
        seen |= h;
        if (members[i].query != QUERY_OK && members[i].query != QUERY_UNSUPPORTED)
            return COMBINATION_UNKNOWN;
    }
    if (seen != combination) return COMBINATION_UNKNOWN;
    for (uint32_t i = 0; i < count; ++i) {
        if (members[i].query == QUERY_UNSUPPORTED ||
            (members[i].features & exportable) != exportable ||
            (members[i].compatible & combination) != combination)
            return COMBINATION_UNSUPPORTED;
    }
    return COMBINATION_SUPPORTED;
}
