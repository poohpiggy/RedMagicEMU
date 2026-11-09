/* Host-side tests of real alloc_any/d_alloc and w_lock_ycbcr; not an ARM ABI test. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define dlopen test_dlopen
#define dlsym test_dlsym
#define open test_open
#include "../gralloc_wrap.c"
#undef dlopen
#undef dlsym
#undef open

static struct gralloc_module mock;
static FILE *backing;
static int unlock_calls, lock_calls, lock_result, failures;
void *test_dlopen(const char *p, int f) { (void)p; (void)f; return &mock; }
void *test_dlsym(void *h, const char *n) { return h == &mock && !strcmp(n, "HMI") ? &mock : NULL; }
int test_open(const char *p, int f, ...) { (void)p; (void)f; return -1; }
static void check(int ok, const char *name) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", name); failures++; }
}
static int mock_lock(const struct gralloc_module *m, const void *h, int usage,
                     int l, int t, int w, int height, void **out) {
    (void)m; (void)usage; (void)l; (void)t; (void)w; (void)height;
    ++lock_calls;
    if (lock_result) return lock_result;
    *out = (void *)(uintptr_t)((const struct aosp_handle *)h)->base;
    return 0;
}
static int mock_unlock(const struct gralloc_module *m, const void *h) {
    (void)m; (void)h; unlock_calls++; return 0;
}
static int mock_alloc(struct alloc_device *d, int w, int h, int format, int usage,
                      const void **out, int *stride) {
    (void)d; (void)usage;
    if (w <= 0 || h <= 0 || format != HAL_PIXEL_FORMAT_RGBA_8888 || backing) return -22;
    size_t len = ((size_t)w * (size_t)h * 4 + 4095) & ~(size_t)4095;
    backing = tmpfile();
    if (!backing || ftruncate(fileno(backing), (off_t)len)) return -12;
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(backing), 0);
    if (p == MAP_FAILED) return -12;
    struct aosp_handle *handle = calloc(1, sizeof(*handle));
    if (!handle) return -12;
    handle->size = (int)len;
    handle->base = (uint64_t)(uintptr_t)p;
    handle->fd = fileno(backing);
    *out = handle;
    *stride = w;
    return 0;
}
static void bad_note(struct aosp_handle *handle, struct yuv_note *note,
                     int width, int height, uint32_t pad, const char *name) {
    struct android_ycbcr ignored = {0};
    int before = unlock_calls;
    note->magic = YUV_NOTE; note->w = width; note->h = height; note->pad = pad;
    int result = w_lock_ycbcr(&HMI, handle, 3, 0, 0, 16, 16, &ignored);
    check(result == -22 && unlock_calls == before + 1, name);
}
static void allocation_roundtrip(int format, int expected_pad) {
    struct alloc_device original = {0};
    original.alloc = mock_alloc;
    struct wrap_device wrapper = {0};
    wrapper.real = &original;
    const void *out_handle = NULL;
    int stride = 0;
    int result = d_alloc(&wrapper.dev, 640, 360, format, 0x33, &out_handle, &stride);
    check(result == 0 && out_handle != NULL && stride == 640, "allocate YUV buffer");
    if (result || !out_handle) return;
    const struct aosp_handle *original_handle = out_handle;
    struct yuv_note *note = note_of(original_handle);
    check(note && note->magic == YUV_NOTE && note->w == 640 && note->h == 360 &&
          note->pad == (uint32_t)expected_pad, "allocator writes correct shared YUV note");
    void *second = mmap(NULL, (size_t)original_handle->size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, original_handle->fd, 0);
    check(second != MAP_FAILED, "second shared mapping succeeds");
    if (second != MAP_FAILED) {
        struct aosp_handle imported = *original_handle;
        imported.base = (uint64_t)(uintptr_t)second;
        struct android_ycbcr planes = {0};
        result = w_lock_ycbcr(&HMI, &imported, 0x33, 0, 0, 640, 360, &planes);
        check(result == 0, "imported shared buffer YCbCr lock succeeds");
        if (!result) {
            size_t y = 640u * 360u;
            size_t chroma = 320u * 180u;
            check(planes.y == second && planes.cr == (char *)second + y &&
                  planes.cb == (char *)second + y + (expected_pad ? chroma : 1) &&
                  planes.ystride == 640 && planes.cstride == (expected_pad ? 320u : 640u) &&
                  planes.chroma_step == (expected_pad ? 1u : 2u), "imported YUV plane layout");
            mock_unlock(&mock, &imported);
        }
        munmap(second, (size_t)original_handle->size);
    }
    munmap((void *)(uintptr_t)original_handle->base, (size_t)original_handle->size);
    free((void *)original_handle);
    fclose(backing);
    backing = NULL;
}

/* A reused YV12 buffer must invalidate the host copy on each successful CPU write. */
static void yv12_write_generation(void) {
    struct alloc_device original = {0};
    struct wrap_device wrapper = {0};
    const void *handle = NULL;
    struct android_ycbcr planes = {0};
    int stride = 0;
    original.alloc = mock_alloc;
    wrapper.real = &original;
    int result = d_alloc(&wrapper.dev, 640, 360, HAL_PIXEL_FORMAT_YV12, 0x33, &handle, &stride);
    check(result == 0 && handle != NULL, "allocate YV12 for write-generation checks");
    if (result || !handle) return;
    const struct aosp_handle *h = handle;
    struct meta_note *meta = meta_note_at(handle);
    struct yuv_note *note = note_of(handle);
    check(meta && meta->magic == META_NOTE && note && note->magic == YUV_NOTE,
          "YV12 metadata is present");
    if (meta && meta->magic == META_NOTE && note && note->magic == YUV_NOTE) {
        uint64_t before = meta->pad[0];
        result = w_lock_ycbcr(&HMI, handle, 0x03, 0, 0, 640, 360, &planes);
        check(result == 0 && meta->pad[0] == before, "YCbCr CPU read leaves generation unchanged");
        if (!result) w_unlock(&HMI, handle);

        before = meta->pad[0];
        result = w_lock_ycbcr(&HMI, handle, 0x30, 0, 0, 640, 360, &planes);
        check(result == 0 && meta->pad[0] == before + 1, "YCbCr CPU write increments generation");
        if (!result) w_unlock(&HMI, handle);

        before = meta->pad[0];
        result = w_lock_ycbcr(&HMI, handle, 0x33, 0, 0, 640, 360, &planes);
        check(result == 0 && meta->pad[0] == before + 1, "YCbCr CPU read/write increments generation");
        if (!result) w_unlock(&HMI, handle);

        before = meta->pad[0];
        note->magic = 0;
        result = w_lock_ycbcr(&HMI, handle, 0x30, 0, 0, 640, 360, &planes);
        check(result == -22 && meta->pad[0] == before, "invalid YUV note leaves generation unchanged");
        note->magic = YUV_NOTE;

        before = meta->pad[0];
        lock_result = -12;
        result = w_lock_ycbcr(&HMI, handle, 0x30, 0, 0, 640, 360, &planes);
        lock_result = 0;
        check(result == -12 && meta->pad[0] == before, "failed underlying lock leaves generation unchanged");
    }
    munmap((void *)(uintptr_t)h->base, (size_t)h->size);
    free((void *)h);
    fclose(backing);
    backing = NULL;
}
/* The forged size maps beyond EOF successfully on Linux; touching its second
 * page raises SIGBUS. Only this self-owned child can fault. */
static void reject_oversized_handle(void) {
    FILE *f = tmpfile();
    if (!f || ftruncate(fileno(f), 4096)) { check(0, "prepare short backing file"); return; }
    void *mapping = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(f), 0);
    if (mapping == MAP_FAILED) { check(0, "map forged size"); fclose(f); return; }
    pid_t child = fork();
    if (!child) {
        struct aosp_handle h = {0};
        struct android_ycbcr out = {0};
        h.fd = fileno(f); h.size = 8192; h.base = (uint64_t)(uintptr_t)mapping;
        int before = lock_calls;
        alarm(5);
        int result = w_lock_ycbcr(&HMI, &h, 3, 0, 0, 16, 16, &out);
        _exit(result == -22 && lock_calls == before ? 0 : 1);
    }
    int status = 0;
    check(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "reject forged size beyond fd before locking or reading note");
    munmap(mapping, 8192);
    fclose(f);
}
int main(void) {
    mock.lock = mock_lock;
    mock.unlock = mock_unlock;
    init();
    allocation_roundtrip(HAL_PIXEL_FORMAT_YV12, 640);
    allocation_roundtrip(HAL_PIXEL_FORMAT_YCBCR_420_888, 0);
    yv12_write_generation();

    FILE *testfile = tmpfile();
    if (!testfile || ftruncate(fileno(testfile), 4096)) return 2;
    unsigned char *pixels = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fileno(testfile), 0);
    if (pixels == MAP_FAILED) return 2;
    struct aosp_handle h = {0};
    h.fd = fileno(testfile); h.size = 4096; h.base = (uint64_t)(uintptr_t)pixels;
    struct yuv_note *n = note_of(&h);
    struct android_ycbcr out = {0};
    n->magic = YUV_NOTE; n->w = 16; n->h = 16; n->pad = 16;
    check(w_lock_ycbcr(&HMI, &h, 3, 0, 0, 16, 16, &out) == 0, "valid YV12 lock");
    check(out.y == pixels && out.cr == pixels + 256 && out.cb == pixels + 384 &&
          out.ystride == 16 && out.cstride == 16 && out.chroma_step == 1, "valid YV12 layout");
    int prior_lock = lock_calls;
    h.offset = 16;
    check(w_lock_ycbcr(&HMI, &h, 3, 0, 0, 16, 16, &out) == -22 && lock_calls == prior_lock,
          "reject nonzero handle offset before locking");
    h.offset = 0;
    prior_lock = lock_calls;
    h.flags = 1; /* framebuffer handles are never allocated for this YUV path */
    check(w_lock_ycbcr(&HMI, &h, 3, 0, 0, 16, 16, &out) == -22 && lock_calls == prior_lock,
          "reject framebuffer handle before locking");
    h.flags = 0;
    bad_note(&h, n, 0, 16, 16, "reject zero width");
    bad_note(&h, n, 16, 0, 16, "reject zero height");
    bad_note(&h, n, -1, 16, 16, "reject negative width");
    bad_note(&h, n, 16, -1, 16, "reject negative height");
    bad_note(&h, n, 32, 16, 16, "reject YV12 stride smaller than width");
    bad_note(&h, n, 16, 16, 17, "reject misaligned YV12 stride");
    bad_note(&h, n, 16, 16, 32, "reject YV12 stride inconsistent with width");
    bad_note(&h, n, 16, 128, 16, "reject YV12 planes overlapping reserved tail");
    bad_note(&h, n, 16, 0x7fffffff, 16, "reject YV12 planes exceeding allocation");
    bad_note(&h, n, 640, 360, 0, "reject NV21 planes exceeding allocation");
    int before = unlock_calls;
    n->magic = 0;
    check(w_lock_ycbcr(&HMI, &h, 3, 0, 0, 16, 16, &out) == -22 && unlock_calls == before + 1,
          "missing note fails and unlocks");
    before = unlock_calls; lock_result = -12;
    check(w_lock_ycbcr(&HMI, &h, 3, 0, 0, 16, 16, &out) == -12 && unlock_calls == before,
          "underlying lock failure propagates without unlock");
    before = lock_calls;
    check(w_lock_ycbcr(&HMI, &h, 3, 0, 0, 16, 16, NULL) == -22 && lock_calls == before,
          "null output fails before locking");
    munmap(pixels, 4096);
    fclose(testfile);
    reject_oversized_handle();
    if (failures) { fprintf(stderr, "%d checks failed\n", failures); return 1; }
    puts("GRALLOC_YUV_HOST_OK");
    return 0;
}
