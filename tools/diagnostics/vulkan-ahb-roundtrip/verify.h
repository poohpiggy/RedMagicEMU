#ifndef RME_AHB_PROBE_VERIFY_H
#define RME_AHB_PROBE_VERIFY_H
#include <stdint.h>
#include <stddef.h>
#define FILL_WORD UINT32_C(0xa1b2c3d4)
static inline uint32_t input_word(uint32_t i) {
    return (i * UINT32_C(0x10203)) ^ UINT32_C(0x739d51af);
}
static inline size_t mismatches(const uint32_t *words, size_t count, int fill, int wrong) {
    size_t errors = 0;
    for (size_t i=0;i<count;++i) {
        uint32_t expected=fill ? FILL_WORD : input_word((uint32_t)i);
        if (wrong && i==count/2) expected^=1;
        if (words[i]!=expected) ++errors;
    }
    return errors;
}
#endif