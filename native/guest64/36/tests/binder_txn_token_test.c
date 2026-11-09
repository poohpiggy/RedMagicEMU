#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../binder_txn_trace.h"
#ifndef BTT_TOKEN_DIAG_DEFINED
#define BTT_TOKEN_DIAG_DEFINED
#define BTT_TOKEN_NONE 0
#define BTT_TOKEN_UNAVAILABLE 1
#define BTT_TOKEN_OTHER 2
#define BTT_TOKEN_MATCH 3
static int btt_token(int enabled, unsigned code, int safe, const void *data, size_t size) {
    (void)enabled; (void)code; (void)safe; (void)data; (void)size; return BTT_TOKEN_UNAVAILABLE;
}
#endif
static void put32(unsigned char *p, uint32_t n) { memcpy(p, &n, sizeof n); }
int main(void) {
    const char name[] = "android.media.IMediaPlayerService";
    unsigned char q[128] = {0};
    put32(q + 8, 0x53595354); put32(q + 12, sizeof name - 1);
    for (size_t i = 0; i < sizeof name; i++) q[16 + i * 2] = (unsigned char)name[i];
    size_t n = 16 + sizeof name * 2;
    if (btt_token(1, 7, 1, q, n) != BTT_TOKEN_MATCH) return 1;
    if (btt_token(0, 7, 1, (void *)1, (size_t)-1) != BTT_TOKEN_NONE) return 2;
    if (btt_token(1, 8, 1, (void *)1, (size_t)-1) != BTT_TOKEN_NONE) return 3;
    if (btt_token(1, 7, 0, (void *)1, (size_t)-1) != BTT_TOKEN_UNAVAILABLE) return 4;
    if (btt_token(1, 7, 1, NULL, n) != BTT_TOKEN_UNAVAILABLE) return 5;
    for (size_t i = 0; i < n; i++) if (btt_token(1, 7, 1, q, i) != BTT_TOKEN_UNAVAILABLE) return 6;
    q[16] = 'X'; if (btt_token(1, 7, 1, q, n) != BTT_TOKEN_OTHER) return 7; q[16] = 'a';
    put32(q + 12, 0xffffffffu); if (btt_token(1, 7, 1, q, n) != BTT_TOKEN_UNAVAILABLE) return 8;
    put32(q + 12, 0x7fffffffu); if (btt_token(1, 7, 1, q, n) != BTT_TOKEN_UNAVAILABLE) return 9;
    put32(q + 12, sizeof name - 1); q[n-1] = 1;
    if (btt_token(1, 7, 1, q, n) != BTT_TOKEN_UNAVAILABLE) return 10;
    q[n-1] = 0; put32(q + 8, 0);
    if (btt_token(1, 7, 1, q, n) != BTT_TOKEN_UNAVAILABLE) return 11;
    return 0;
}
