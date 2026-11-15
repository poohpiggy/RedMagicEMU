/* Exercise the production lock/fetch path over a real local stream socket. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

extern int socketpair(int, int, int, int[2]);
extern int shutdown(int, int);

#define dlopen test_dlopen
#define dlsym test_dlsym
#include "../gralloc_wrap.c"
#undef dlopen
#undef dlsym

static struct gralloc_module mock;
static int unlock_calls, failures;
void *test_dlopen(const char *p, int f) { (void)p; (void)f; return &mock; }
void *test_dlsym(void *h, const char *n) { return h == &mock && !strcmp(n, "HMI") ? &mock : NULL; }
static int mock_lock(const struct gralloc_module *m, const void *h, int usage,
                     int l, int t, int w, int height, void **out) {
    (void)m; (void)usage; (void)l; (void)t; (void)w; (void)height;
    *out = (void *)(uintptr_t)((const struct rgb_note_handle *)h)->base;
    return 0;
}
static int mock_unlock(const struct gralloc_module *m, const void *h) {
    (void)m; (void)h; unlock_calls++; return 0;
}
static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); failures++; }
}

static void run_case(const char *name, int format, uint32_t status, size_t payload,
                     int expect_error) {
    int fd[2];
    if (socketpair(1, 1, 0, fd)) abort();
    unsigned char *memory = malloc(4096);
    if (!memory) abort();
    memset(memory, 0xa5, 4096);
    struct rgb_note_handle h = {0};
    h.size = 4096; h.base = (uint64_t)(uintptr_t)memory;
    struct meta_note *n = meta_note_at(&h);
    memset(n, 0, sizeof(*n));
    n->magic = META_NOTE; n->id = 1; n->w = 3; n->h = 2; n->stride = 5;
    n->format = format; n->pad[1] = 1;
    unsigned char pixels[24];
    for (size_t i = 0; i < sizeof(pixels); i++) pixels[i] = (unsigned char)(i + 1);
    if (write(fd[1], &status, sizeof(status)) != sizeof(status) ||
        (payload && write(fd[1], pixels, payload) != (long)payload)) abort();
    if (shutdown(fd[1], 1)) abort();
    g_gl = fd[0]; g_gl_pid = getpid();
    int before = unlock_calls;
    void *out = (void *)1;
    int result = w_lock(&HMI, &h, 3, 0, 0, 3, 2, &out);
    if (expect_error) {
        check(result != 0, name);
        check(out == NULL, "failed lock clears returned pointer");
        check(unlock_calls == before + 1, "failed lock releases real lock");
        check(g_gl == -1 && fcntl(fd[0], F_GETFD) == -1, "truncated frame closes socket");
        for (int i = 0; i < 40; i++) check(memory[i] == 0xa5, "truncated frame preserves old pixels and padding");
    } else {
        check(result == 0 && out == memory && unlock_calls == before, name);
        if (status == 0) {
            for (int i = 0; i < 40; i++) check(memory[i] == 0xa5, "no-update reply keeps old pixels");
        } else if (format == 1) {
            check(!memcmp(memory, pixels, 12) && !memcmp(memory + 20, pixels + 12, 12), "complete RGBA rows");
            for (int i = 12; i < 20; i++) check(memory[i] == 0xa5, "RGBA row padding preserved");
        } else {
            const uint16_t *row0 = (const uint16_t *)memory;
            const uint16_t *row1 = (const uint16_t *)(memory + 10);
            for (int i = 0; i < 6; i++) {
                unsigned char *p = pixels + i * 4;
                uint16_t expected = (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3);
                check((i < 3 ? row0[i] : row1[i - 3]) == expected, "complete RGB565 conversion");
            }
            for (int i = 6; i < 10; i++) check(memory[i] == 0xa5, "RGB565 row padding preserved");
        }
    }
    if (g_gl >= 0) close(g_gl);
    g_gl = -1;
    close(fd[1]);
    free(memory);
}
int main(void) {
    mock.lock = mock_lock; mock.unlock = mock_unlock; real = &mock;
    run_case("RGBA truncated mid second row fails", 1, 1, 17, 1);
    run_case("RGB565 truncated mid second row fails", 4, 1, 17, 1);
    run_case("RGBA empty payload fails", 1, 1, 0, 1);
    run_case("RGBA complete frame succeeds", 1, 1, 24, 0);
    run_case("RGB565 complete frame succeeds", 4, 1, 24, 0);
    run_case("no-update response succeeds", 1, 0, 0, 0);
    if (failures) fprintf(stderr, "%d checks failed\n", failures);
    else puts("gpu_fetch tests passed");
    return failures != 0;
}
