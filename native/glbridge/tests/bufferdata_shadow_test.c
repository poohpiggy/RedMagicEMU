/* Tests the production encoder and index shadow without a GL host. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../guest.c"

int __android_log_print(int priority, const char *tag, const char *format, ...) {
    (void)priority; (void)tag; (void)format; return 0;
}
int __system_property_get(const char *name, char *value) {
    (void)name; value[0] = 0; return 0;
}
static uint32_t load32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static int forwarded(struct ts *t, uint32_t start, GLenum usage, const uint8_t *data) {
    const uint8_t *p = t->buf + start;
    /* u32 opcode, u32 payload size, target, u64 size, blob size, data padded, usage */
    return t->n == start + 32 && load32(p) == IDX_glBufferData && load32(p+4) == 24 &&
           load32(p+8) == 0x8893 && load32(p+12) == 3 && load32(p+16) == 0 &&
           load32(p+20) == 3 && !memcmp(p+24, data, 3) && load32(p+28) == usage;
}
int main(void) {
    static const GLenum legal[] = { 0x88e0, 0x88e1, 0x88e2, 0x88e4, 0x88e5,
                                    0x88e6, 0x88e8, 0x88e9, 0x88ea };
    static const GLenum invalid[] = { 0, 0x88e3, 0x88e7, 0x88eb, 0xffffffffu };
    struct ectx context = {0};
    struct ts stream = {0};
    uint8_t encoded[4096];
    int failures = 0;
    context.g.root = 42;
    stream.ctx = &context; stream.buf = encoded; stream.cap = sizeof(encoded); stream.fd = -1;
    t_ts = &stream;
    e_glBindBuffer(0x8893, 7);
    for (size_t i=0; i<sizeof(legal)/sizeof(legal[0]); ++i) {
        uint8_t data[3] = { 1, 2, (uint8_t)(10+i) };
        uint32_t start = stream.n;
        e_glBufferData(0x8893, sizeof(data), data, legal[i]);
        if (sh_max(&context.g, 0, 3, 1) != data[2]) {
            fprintf(stderr, "FAIL legal usage 0x%x did not update shadow\n", legal[i]); ++failures;
        }
        if (!forwarded(&stream, start, legal[i], data)) {
            fprintf(stderr, "FAIL legal usage 0x%x not forwarded intact\n", legal[i]); ++failures;
        }
    }
    for (size_t i=0; i<sizeof(invalid)/sizeof(invalid[0]); ++i) {
        const uint8_t data[3] = { 80, 81, 82 };
        uint32_t start = stream.n;
        e_glBufferData(0x8893, sizeof(data), data, invalid[i]);
        if (sh_max(&context.g, 0, 3, 1) != 18) {
            fprintf(stderr, "FAIL invalid usage 0x%x changed shadow\n", invalid[i]); ++failures;
        }
        if (!forwarded(&stream, start, invalid[i], data)) {
            fprintf(stderr, "FAIL invalid usage 0x%x not forwarded intact\n", invalid[i]); ++failures;
        }
    }
    sh_drop_all(context.g.root);
    t_ts = NULL;
    if (failures) return 1;
    puts("BUFFERDATA_SHADOW_OK"); return 0;
}
