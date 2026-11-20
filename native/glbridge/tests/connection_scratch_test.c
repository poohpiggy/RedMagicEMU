/* Android host-side regression: drive the real connection loop with two scratch allocations. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { WIDTH = 912, HEIGHT = 2016, FRAME_BYTES = WIDTH * HEIGHT * 4, STAGE_BYTES = 2304 * 1024 };
static void *frame_allocation, *stage_allocation;
static int frame_seen, stage_seen;

static void *track_malloc(size_t size) {
    void *p = malloc(size);
    if (size == FRAME_BYTES) { frame_allocation = p; frame_seen = p != NULL; }
    if (size == STAGE_BYTES) { stage_allocation = p; stage_seen = p != NULL; }
    return p;
}

static void track_free(void *p) {
    if (p == frame_allocation) frame_allocation = NULL;
    if (p == stage_allocation) stage_allocation = NULL;
    free(p);
}

#define malloc track_malloc
#define free track_free
#define glFinish allocate_upload_scratch
#include "../host.c"
#undef glFinish
#undef free
#undef malloc

/* The ping invokes the upload fallback on the serving thread, with no current EGL context. */
void allocate_upload_scratch(void) {
    struct hctx context = {0};
    uint8_t *bytes = NULL;
    int upload = 0;
    assert(ring_take(&context, STAGE_BYTES, &bytes, &upload) == 0 && upload && bytes);
    memset(bytes, 0x5a, STAGE_BYTES);
}

int main(void) {
    int pair[2];
    pthread_t thread;
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(pthread_create(&thread, NULL, serve, (void *)(intptr_t)pair[1]) == 0);

    const uint32_t requests[] = {OP_SWAP_FMT, 16, WIDTH, HEIGHT, 1, WIDTH, OP_PING, 0};
    assert(write(pair[0], requests, sizeof requests) == sizeof requests);
    assert(shutdown(pair[0], SHUT_WR) == 0);
    ssize_t count;
    size_t received = 0;
    char buffer[8192];
    while ((count = read(pair[0], buffer, sizeof buffer)) > 0) received += (size_t)count;
    assert(count == 0 && received == FRAME_BYTES + sizeof(uint32_t));
    close(pair[0]);
    assert(pthread_join(thread, NULL) == 0);
    assert(frame_seen && stage_seen);

    if (frame_allocation || stage_allocation) {
        fprintf(stderr, "Scratch retained after disconnect: frame=%d bytes, upload=%d bytes\n",
                frame_allocation ? FRAME_BYTES : 0, stage_allocation ? STAGE_BYTES : 0);
        return 1;
    }
    puts("Frame and upload scratch released after disconnect");
    return 0;
}
