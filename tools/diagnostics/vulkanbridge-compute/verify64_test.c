#include "../vulkan-compute/verify.h"
#include <stdio.h>
int main(void) {
    uint32_t words[64];
    for(unsigned i=0;i<64;i++) words[i]=i*3u+7u;
    if(verify_words(words,64,0)!=0 || verify_words(words,64,1)!=1) return 1;
    for(unsigned i=0;i<64;i++) {
        words[i]++;
        if(verify_words(words,64,0)!=1) return 1;
        words[i]--;
    }
    puts("VERIFY_64_GOOD_AND_EACH_BAD_OK (host validator only)");
    return 0;
}
