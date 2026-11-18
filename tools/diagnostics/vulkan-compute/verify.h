#ifndef RME_VK_VERIFY_H
#define RME_VK_VERIFY_H
#include <stddef.h>
#include <stdint.h>
static inline size_t verify_words(const uint32_t *words, size_t count, int inject_bad_expectation) {
    size_t errors = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t expected = (uint32_t)i * 3u + 7u;
        if (inject_bad_expectation && i == count / 2) ++expected;
        if (words[i] != expected) ++errors;
    }
    return errors;
}
#endif
