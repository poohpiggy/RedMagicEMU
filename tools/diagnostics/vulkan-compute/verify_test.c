#include "verify.h"
#include <stdio.h>
int main(void) {
    uint32_t good[4] = {7, 10, 13, 16};
    uint32_t bad[4] = {7, 10, 99, 16};
    if (verify_words(good, 4, 0) != 0 || verify_words(bad, 4, 0) != 1 ||
        verify_words(good, 4, 1) != 1) {
        fputs("FAIL: output validator must reject a bad GPU result\n", stderr);
        return 1;
    }
    puts("VERIFY_WORDS_OK");
    return 0;
}
